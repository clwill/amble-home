#include "amble.h"

#include <cstring>

#include "esphome/components/esp32_ble/ble.h"
#include "esphome/components/network/util.h"
#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_random.h>
#include <lwip/ip_addr.h>
#include <mdns.h>

namespace esphome {
namespace amble {

static const char *const TAG = "amble";
static constexpr uint32_t STATUS_TIMEOUT_MS = 1000;  // one retry, then leave state as commanded
static constexpr uint32_t DIAG_PERIOD_MS = 5000;
static constexpr uint32_t LIVE_WINDOW_MS = 60000;
static constexpr uint32_t ENTITY_DIAGNOSTIC = 2u << 26;  // ENTITY_FIELD_ENTITY_CATEGORY_SHIFT
static constexpr size_t STATUS_QUEUE_LEN = 16;

// Logs identify a fixture by its slot ("fixture 3"), never by name, MAC or address
// (standing rule 2, strict reading).

void AmbleComponent::setup() {
  this->guard_ = TransmitGuard(this->allow_group_, this->release_);
  this->guard_.set_allowlist(this->dev_allowlist_.data(), this->dev_allowlist_.size());
  this->status_queue_ = xQueueCreate(STATUS_QUEUE_LEN, sizeof(StatusEvent));
  this->poll_ = PollScheduler(this->status_poll_ms_, 150);
  this->has_config_ = this->store_.load(&this->config_);
  this->load_pin_();
  if (this->has_config_) {
    // The box's own mesh address is never imported: it comes from this box's
    // address seed and the table it runs (#25). The persisted value is
    // overwritten here, so a configuration saved under an older scheme migrates
    // on its next boot. Without a seed there is no identity and the mesh stays
    // down (own_address 0: MeshNode::start refuses it).
    this->config_.own_address =
        this->load_or_make_seed_() ? derive_own_address(this->address_seed_, this->config_.fixtures, this->config_.fixture_count) : 0;
    ESP_LOGI(TAG, "configuration loaded: %u fixture(s); this box is mesh node 0x%04X", this->config_.fixture_count,
             this->config_.own_address);
    // Release/production: the guard may address exactly this table (fixed until the next boot).
    uint16_t addrs[MAX_FIXTURES];
    for (uint8_t i = 0; i < this->config_.fixture_count; i++)
      addrs[i] = this->config_.fixtures[i].address;
    this->guard_.set_configured(addrs, this->config_.fixture_count);
    this->create_entities_();
  } else {
    ESP_LOGW(TAG, "no configuration: waiting for setup (POST /amble/import)");
  }
  this->create_diagnostics_();
  this->mesh_.set_store(&this->nvs_);
  this->mesh_.set_access_handler(&AmbleComponent::on_access_, this);
  this->mesh_.set_source_filter(&AmbleComponent::is_our_fixture_, this);
  this->update_snapshot_();
}

// Random on first boot, then fixed for the life of the box; an erase makes a new
// one, so an erased box almost always comes back at an address it never used
// (fourteen bits of seed: a 1-in-16384 chance per earlier identity of landing on
// one the fixtures remember with a restarted sequence number; accepted residual,
// see docs/decisions.md). Fails closed: a seed that cannot be read or stored
// would change on the next boot.
bool AmbleComponent::load_or_make_seed_() {
  static constexpr const char *SEED_KEY = "seed";
  uint16_t seed = 0;
  size_t len = sizeof(seed);
  switch (this->nvs_.probe(SEED_KEY)) {
    case StoreBackend::Probe::PRESENT:
      if (!this->nvs_.read(SEED_KEY, &seed, &len) || len != sizeof(seed) || seed == 0) {
        ESP_LOGE(TAG, "the box's address seed is unreadable; the mesh stays down");
        return false;
      }
      break;
    case StoreBackend::Probe::ABSENT:
      while (seed == 0)
        esp_fill_random(&seed, sizeof(seed));
      if (!this->nvs_.write(SEED_KEY, &seed, sizeof(seed)) || !this->nvs_.commit()) {
        ESP_LOGE(TAG, "could not store the box's address seed; the mesh stays down");
        return false;
      }
      ESP_LOGI(TAG, "address seed chosen (first boot)");
      break;
    default:
      ESP_LOGE(TAG, "could not read the box's storage for its address seed; the mesh stays down");
      return false;
  }
  this->address_seed_ = seed;
  return true;
}

bool AmbleComponent::is_our_fixture_(uint16_t src, void *ctx) {
  // Bluetooth task: reads the fixture table, which only changes across a reboot.
  auto *self = static_cast<AmbleComponent *>(ctx);
  if (self->index_for_address_(src) >= 0)
    return true;
  self->unknown_sources_.fetch_add(1, std::memory_order_relaxed);
  return false;
}

void AmbleComponent::create_entities_() {
  for (uint8_t i = 0; i < this->config_.fixture_count; i++) {
    const Fixture &f = this->config_.fixtures[i];
    std::string oid = object_id_for(f);
    uint32_t hash = fnv1_hash(oid);

    auto *out = new AmbleLight(this, i);  // NOLINT: lives for the life of the device
    auto *st = new light::LightState(out);  // NOLINT
    out->set_state(st);
    st->set_default_transition_length(0);
    st->set_flash_transition_length(0);
    st->set_gamma_correct(1.0f);
    App.register_light(st, f.name, hash, 0);
    // ESPHome has no public way to register a component created at boot, so this
    // component drives each LightState's setup()/loop() itself (see loop()).
    this->lights_.push_back(out);
    this->light_states_.push_back(st);
    this->light_setup_done_.push_back(false);

    this->names_.push_back(std::string(f.name) + " green/magenta");
    auto *gm = new AmbleGmNumber(this, i);  // NOLINT
    gm->traits.set_min_value(-10);
    gm->traits.set_max_value(10);
    gm->traits.set_step(1);
    App.register_number(gm, this->names_.back().c_str(), fnv1_hash(oid + "_gm"), f.caps.gm ? 0u : (1u << 24));
    gm->publish_state(0);
    this->gm_numbers_.push_back(gm);

    this->names_.push_back(std::string(f.name) + " identify");
    auto *btn = new AmbleIdentifyButton(this, i);  // NOLINT
    App.register_button(btn, this->names_.back().c_str(), fnv1_hash(oid + "_identify"), 0);
    this->identify_buttons_.push_back(btn);
  }
  this->last_seen_ms_.assign(this->config_.fixture_count, 0);
  this->pending_retry_.assign(this->config_.fixture_count, 0);
}

void AmbleComponent::create_diagnostics_() {
  // Counters carry a unit ("msg") and a total-increasing state class so Home
  // Assistant graphs them and keeps them out of the logbook; a unit-less number
  // is logged on every change, which drowned the logbook in mesh ticks (#22).
  // The IV index is the one value whose change deserves a logbook line: no unit.
  auto mk = [&](const char *name, const char *suffix, bool counter) {
    auto *s = new sensor::Sensor();  // NOLINT
    s->set_accuracy_decimals(0);
    uint32_t fields = ENTITY_DIAGNOSTIC;
    if (counter) {
      fields |= (uint32_t) AMBLE_UOM_MSG_IDX << ENTITY_FIELD_UOM_SHIFT;
      s->set_state_class(sensor::STATE_CLASS_TOTAL_INCREASING);
    }
    App.register_sensor(s, name, fnv1_hash(std::string("amble_diag_") + suffix), fields);
    return s;
  };
  this->sensor_sent_ = mk("Mesh commands sent", "sent", true);
  this->sensor_replies_ = mk("Mesh status replies", "replies", true);
  this->sensor_seq_ = mk("Mesh sequence number", "seq", true);
  this->sensor_iv_ = mk("Mesh IV index", "iv", false);
  // The release tripwire (docs/decisions.md): the stack refused to queue a message. Must stay 0.
  this->sensor_send_fail_ = mk("Mesh send failures", "send_fail", true);
  this->sensor_timeouts_ = mk("Mesh status timeouts", "timeouts", true);
  this->sensor_denied_ = mk("Mesh commands refused", "denied", true);
  // Decrypted replies from addresses not in the table: fixtures not imported, or another node.
  this->sensor_unknown_ = mk("Mesh unknown sources", "unknown", true);

  this->mesh_up_ = new binary_sensor::BinarySensor();  // NOLINT
  App.register_binary_sensor(this->mesh_up_, "Mesh up", fnv1_hash("amble_diag_up"), ENTITY_DIAGNOSTIC);

  this->resync_button_ = new AmbleResyncButton(this);  // NOLINT
  App.register_button(this->resync_button_, "Re-sync fixtures", fnv1_hash("amble_resync"), ENTITY_DIAGNOSTIC);
}

void AmbleComponent::resync() {
  // Ask every fixture for its state now (the guard still decides who may be asked).
  if (!this->mesh_.started())
    return;
  for (uint8_t i = 0; i < this->config_.fixture_count; i++)
    if (this->pending_retry_[i] == 0)
      this->request_status_after(i, 100u * i);
}

// Answer `alias.local` too, so a one-box home never needs the MAC suffix. The
// mdns responder only takes a delegated name once it is running and the box has
// an address; both are re-checked on every change of IP (and retried on failure).
void AmbleComponent::serve_mdns_alias_() {
  if (this->mdns_alias_.empty() || this->mdns_alias_ == App.get_name().c_str())
    return;
  if (!network::is_connected())
    return;
  uint32_t ip = 0;
  for (const auto &a : network::get_ip_addresses()) {
    if (a.is_set() && a.is_ip4()) {
      ip_addr_t raw = a;
      ip = ip_addr_get_ip4_u32(&raw);  // lwip: right in both IPv4-only and dual-stack builds
      break;
    }
  }
  if (ip == 0 || ip == this->mdns_alias_ip_)
    return;
  uint32_t now = millis();
  if (this->mdns_alias_retry_ms_ != 0 && now - this->mdns_alias_retry_ms_ < 5000)
    return;
  this->mdns_alias_retry_ms_ = now;
  if (this->mdns_alias_ip_ != 0) {
    // The responder returns OK for adding a name it already has, so a failed
    // removal must stop here (and retry) or the alias would keep the old address.
    if (mdns_delegate_hostname_remove(this->mdns_alias_.c_str()) != ESP_OK)
      return;
    this->mdns_alias_ip_ = 0;
  }
  mdns_ip_addr_t entry = {};
  entry.addr.type = ESP_IPADDR_TYPE_V4;
  entry.addr.u_addr.ip4.addr = ip;
  esp_err_t err = mdns_delegate_hostname_add(this->mdns_alias_.c_str(), &entry);
  if (err == ESP_OK) {
    this->mdns_alias_ip_ = ip;
    ESP_LOGI(TAG, "also answering as %s.local", this->mdns_alias_.c_str());
  } else if (err != ESP_ERR_INVALID_STATE) {  // INVALID_STATE: the responder is not up yet; retried
    ESP_LOGW(TAG, "mDNS alias %s.local not registered: %s", this->mdns_alias_.c_str(), esp_err_to_name(err));
  }
}

void AmbleComponent::loop() {
  this->serve_mdns_alias_();
  // Drive each runtime LightState. Its first call() runs LightState::setup(),
  // whose state restore writes to the output: suppressed, it is not a command.
  for (size_t i = 0; i < this->light_states_.size(); i++) {
    if (!this->light_setup_done_[i]) {
      // setup() schedules the restore write; the following loop() performs it.
      // Both happen here, inside the suppression window, before any command.
      this->lights_[i]->set_in_setup(true);
      this->light_states_[i]->call();  // setup
      this->light_states_[i]->call();  // loop: consumes the deferred restore write
      this->lights_[i]->set_in_setup(false);
      this->light_setup_done_[i] = true;
    } else {
      this->light_states_[i]->call();
    }
  }
  if (this->has_config_ && !this->mesh_.started() && !this->mesh_.failed()) {
    if (esp32_ble::global_ble != nullptr && esp32_ble::global_ble->is_active()) {
      if (this->mesh_.start(this->config_)) {
        ESP_LOGI(TAG, "mesh ready: commands from Home Assistant now reach the fixtures");
        // Learn each fixture's real state once, staggered: one unicast status request
        // per fixture (the spec's sparse liveness request), so entities are not stale
        // until a fixture happens to speak.
        for (uint8_t i = 0; i < this->config_.fixture_count; i++)
          this->request_status_after(i, 500 + 150u * i);
        this->poll_.reset(this->config_.fixture_count, millis());
      } else
        ESP_LOGE(TAG, "mesh unavailable: %s", this->mesh_.failure_text());
    }
  }
  this->drain_status_queue_();
  if (this->mesh_.started()) {
    // Sparse liveness/status poll (spec "Light semantics"): one request at a time,
    // never while that fixture already has one outstanding.
    int due = this->poll_.next_due(millis(), this->pending_retry_.data());
    if (due >= 0)
      this->request_status_((uint8_t) due, false);
  }
  this->process_pending_import_();
  this->process_pending_pin_();
  this->process_pending_cloud_();
  int ident = this->pending_identify_.exchange(-1);
  if (ident >= 0)
    this->identify((uint8_t) ident);
  bool periodic = millis() - this->last_diag_ms_ > DIAG_PERIOD_MS;
  if (periodic) {
    this->last_diag_ms_ = millis();
    // Publish only what changed: HA keeps every update, and an unchanged value
    // is noise on the wire and in its history.
    auto pub = [](sensor::Sensor *s, float v) {
      if (!s->has_state() || s->state != v)
        s->publish_state(v);
    };
    pub(this->sensor_sent_, this->sent_total_);
    pub(this->sensor_replies_, this->replies_total_);
    pub(this->sensor_seq_, this->mesh_.seq());
    pub(this->sensor_iv_, this->mesh_.iv_index());
    pub(this->sensor_send_fail_, this->send_failures_);
    pub(this->sensor_timeouts_, this->status_timeouts_);
    pub(this->sensor_denied_, this->denied_total_);
    pub(this->sensor_unknown_, this->unknown_sources_.load(std::memory_order_relaxed));
    if (!this->mesh_up_->has_state() || this->mesh_up_->state != this->mesh_.started())
      this->mesh_up_->publish_state(this->mesh_.started());
    this->update_snapshot_();
  }
}

void AmbleComponent::dump_config() {
  ESP_LOGCONFIG(TAG,
                "amble-home:\n"
                "  configuration: %s, %u fixture(s)\n"
                "  build: %s, dev allowlist entries: %u, configured addresses: %u\n"
                "  mesh: %s%s, seq %lu, iv %lu\n"
                "  status poll every %lu ms\n"
                "  commands sent %lu, denied %lu, status replies %lu, status timeouts %lu",
                this->has_config_ ? "loaded" : "none", this->config_.fixture_count,
                this->guard_.mode(), (unsigned) this->guard_.allowlist_size(),
                (unsigned) this->guard_.configured_size(), this->mesh_.started() ? "up" : "down",
                this->mesh_.restored_from_nvs() ? " (restored from NVS)" : "", (unsigned long) this->mesh_.seq(),
                (unsigned long) this->mesh_.iv_index(), (unsigned long) this->status_poll_ms_,
                (unsigned long) this->sent_total_, (unsigned long) this->denied_total_,
                (unsigned long) this->replies_total_, (unsigned long) this->status_timeouts_);
}

// ---- the single guarded transmit path -----------------------------------------------
bool AmbleComponent::send(uint8_t index, const uint8_t payload[telink::PAYLOAD_LEN]) {
  if (index >= this->config_.fixture_count)
    return false;
  const Fixture &f = this->config_.fixtures[index];
  Deny d = this->guard_.check(f.address);
  if (d != Deny::ALLOWED) {
    this->denied_total_++;
    ESP_LOGW(TAG, "fixture %u: send refused: %s", index, TransmitGuard::text(d));
    return false;
  }
  bool ok = this->mesh_.send_vendor(f.address, payload);
  if (ok)
    this->sent_total_++;
  else
    this->send_failures_++;
  ESP_LOGD(TAG, "fixture %u: sent type 0x%02x (%s)", index, payload[9], ok ? "ok" : "failed");
  return ok;
}

void AmbleComponent::request_status_(uint8_t index, bool is_retry) {
  uint8_t p[telink::PAYLOAD_LEN];
  telink::status_request(p);
  this->send(index, p);
  this->poll_.mark(index, millis());
  this->pending_retry_[index] = is_retry ? 2 : 1;
  this->set_timeout(timer_id(TIMER_STATUS_WAIT, index), STATUS_TIMEOUT_MS, [this, index, is_retry]() {
    if (index >= this->pending_retry_.size() || this->pending_retry_[index] == 0)
      return;  // a reply arrived
    if (!is_retry) {
      this->request_status_(index, true);
    } else {
      this->pending_retry_[index] = 0;
      this->status_timeouts_++;
      ESP_LOGW(TAG, "fixture %u: no status reply after retry; state left as commanded", index);
    }
  });
}

void AmbleComponent::request_status_after(uint8_t index, uint32_t delay_ms) {
  this->set_timeout(timer_id(TIMER_STATUS_REQ, index), delay_ms, [this, index]() { this->request_status_(index, false); });
}

void AmbleComponent::publish_last_known(uint8_t index) {
  // Put the entity back to what the fixture last reported (off if never heard).
  // ESPHome has already overwritten remote_values with the rejected target, so
  // the light keeps its own copy of the last report.
  if (index >= this->lights_.size())
    return;
  auto *st = this->lights_[index]->state();
  if (st == nullptr)
    return;
  const light::LightColorValues &v = this->lights_[index]->last_reported();
  st->current_values = v;
  st->remote_values = v;
  st->publish_state();
}

void AmbleComponent::set_gm(uint8_t index, int gm) {
  if (index >= this->lights_.size())
    return;
  AmbleLight *l = this->lights_[index];
  l->set_gm(gm);
  auto *st = l->state();
  if (st != nullptr && this->mesh_.started() && st->current_values.is_on() &&
      st->current_values.get_color_mode() != light::ColorMode::RGB) {
    l->realise(st->current_values);  // re-send the CCT command with the new tint
    this->request_status_after(index, 300);
  }
}

void AmbleComponent::identify(uint8_t index) {
  if (index >= this->lights_.size())
    return;
  this->lights_[index]->begin_identify();
  this->set_timeout(timer_id(TIMER_IDENTIFY, index), 800, [this, index]() {
    if (index < this->lights_.size())
      this->lights_[index]->end_identify();
  });
}

// ---- status pipeline -------------------------------------------------------------------
void AmbleComponent::on_access_(const AccessMessage &msg, void *ctx) {
  // Bluetooth task. Decode and enqueue; the main loop applies it.
  auto *self = static_cast<AmbleComponent *>(ctx);
  const uint8_t *p = telink::payload_in_access_pdu(msg.data, msg.len);
  if (p == nullptr)
    return;
  StatusEvent ev{msg.src, {}};
  if (!telink::decode_status(p, &ev.status))
    return;
  if (self->status_queue_ != nullptr)
    xQueueSend(self->status_queue_, &ev, 0);  // full queue: drop, the next reply catches up
}

void AmbleComponent::drain_status_queue_() {
  StatusEvent ev;
  while (this->status_queue_ != nullptr && xQueueReceive(this->status_queue_, &ev, 0) == pdTRUE) {
    int idx = this->index_for_address_(ev.src);
    if (idx < 0)
      continue;  // another node's fixture
    this->replies_total_++;
    this->last_seen_ms_[idx] = millis();
    this->pending_retry_[idx] = 0;
    this->lights_[idx]->apply_status(ev.status);
    if (!ev.status.is_hs)
      this->gm_numbers_[idx]->publish_state(ev.status.gm);
    this->update_snapshot_();
  }
}

int AmbleComponent::index_for_address_(uint16_t address) const {
  for (uint8_t i = 0; i < this->config_.fixture_count; i++)
    if (this->config_.fixtures[i].address == address)
      return i;
  return -1;
}

// ---- import (HTTP task validates and queues; main loop commits) --------------------------
bool AmbleComponent::import_config(const char *json, size_t len, ParseError *err) {
  ImportState st = this->import_state_.load();
  if (st == ImportState::QUEUED || st == ImportState::COMMITTED) {
    *err = ParseError::OK;
    return false;  // busy until the reboot
  }
  auto *c = new Config();  // NOLINT: handed to loop(), freed there
  *err = parse_import_payload(json, len, c);
  if (*err != ParseError::OK) {
    delete c;  // NOLINT
    return false;
  }
  this->import_error_.store("");
  this->import_state_.store(ImportState::QUEUED);
  this->pending_import_.store(c);
  return true;
}

void AmbleComponent::process_pending_import_() {
  Config *c = this->pending_import_.exchange(nullptr);
  if (c == nullptr)
    return;
  if (this->store_.commit(*c)) {
    ESP_LOGI(TAG, "import: %u fixture(s) committed; rebooting to build entities", c->fixture_count);
    this->import_state_.store(ImportState::COMMITTED);
    this->set_timeout(TIMER_REBOOT, 1500, []() { App.safe_reboot(); });
  } else {
    ESP_LOGE(TAG, "import: NVS commit failed; active configuration unchanged");
    this->import_error_.store("could not store the configuration");
    this->import_state_.store(ImportState::FAILED);
  }
  delete c;  // NOLINT
  this->update_snapshot_();
}

// ---- Edit-page PIN and identify (HTTP task asks; main loop acts) ---------------------------
static const char *const PIN_KEY = "pin";

void AmbleComponent::load_pin_() {
  char buf[PinGate::MAX_LEN + 1] = {};
  size_t len = PinGate::MAX_LEN;
  if (this->nvs_.read(PIN_KEY, buf, &len) && len <= PinGate::MAX_LEN) {
    buf[len] = '\0';
    std::lock_guard<std::mutex> lock(this->pin_mutex_);
    this->pin_.set(buf);
  }
}

PinGate::Result AmbleComponent::check_pin(const char *candidate) {
  std::lock_guard<std::mutex> lock(this->pin_mutex_);
  return this->pin_.check(candidate, millis());
}

uint32_t AmbleComponent::pin_locked_ms() {
  std::lock_guard<std::mutex> lock(this->pin_mutex_);
  return this->pin_.locked_for_ms(millis());
}

bool AmbleComponent::queue_pin(const char *pin) {
  if (!PinGate::valid_format(pin))
    return false;
  std::lock_guard<std::mutex> lock(this->pin_mutex_);
  strncpy(this->pending_pin_, pin, PinGate::MAX_LEN);
  this->pending_pin_[PinGate::MAX_LEN] = '\0';
  this->pin_error_.store("");
  this->pin_busy_.store(true);  // cleared by the loop after the NVS result, success or not
  this->pin_pending_.store(true);
  return true;
}

void AmbleComponent::process_pending_pin_() {
  if (!this->pin_pending_.load())
    return;
  char buf[PinGate::MAX_LEN + 1];
  {
    std::lock_guard<std::mutex> lock(this->pin_mutex_);
    strcpy(buf, this->pending_pin_);
    this->pin_pending_.store(false);
  }
  bool clear = strcmp(buf, "0000") == 0;
  // Store first; the active PIN changes only once NVS has it, so what the page
  // is told matches what the next boot will do.
  bool ok = clear ? (this->nvs_.erase(PIN_KEY) && this->nvs_.commit())
                  : (this->nvs_.write(PIN_KEY, buf, strlen(buf)) && this->nvs_.commit());
  if (ok) {
    std::lock_guard<std::mutex> lock(this->pin_mutex_);
    this->pin_.set(buf);
    this->pin_error_.store("");
    ESP_LOGI(TAG, "edit PIN %s", clear ? "removed" : "set");
  } else {
    this->pin_error_.store("could not store the PIN");
    ESP_LOGE(TAG, "edit PIN: NVS write failed; previous PIN kept");
  }
  this->pin_busy_.store(false);
  this->update_snapshot_();
}

int AmbleComponent::identify_index(const char *object_id) const {
  if (object_id == nullptr)
    return -1;
  for (uint8_t i = 0; i < this->config_.fixture_count; i++)
    if (object_id_for(this->config_.fixtures[i]) == object_id)
      return i;
  return -1;
}

// ---- status snapshot (built on the main loop, read by the HTTP task) ----------------------
static void json_escape_into(std::string &out, const char *s) {
  for (const unsigned char *p = (const unsigned char *) s; *p; p++) {
    switch (*p) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      default:
        if (*p < 0x20) {
          char buf[8];
          snprintf(buf, sizeof(buf), "\\u%04x", *p);
          out += buf;
        } else {
          out += (char) *p;
        }
    }
  }
}

void AmbleComponent::update_snapshot_() {
  std::string s = "{\"name\":\"";
  json_escape_into(s, App.get_name().c_str());
  s += "\",\"configured\":";
  s += this->has_config_ ? "true" : "false";
  s += ",\"mesh\":{\"up\":";
  s += this->mesh_.started() ? "true" : "false";
  s += ",\"own\":" + std::to_string(this->has_config_ ? this->config_.own_address : 0);
  if (this->mesh_.failed()) {
    s += ",\"error\":\"";
    json_escape_into(s, this->mesh_.failure_text());
    s += "\"";
  }
  s += ",\"seq\":" + std::to_string(this->mesh_.seq()) + ",\"iv\":" + std::to_string(this->mesh_.iv_index());
  s += ",\"sent\":" + std::to_string(this->sent_total_) + ",\"replies\":" + std::to_string(this->replies_total_) +
       ",\"denied\":" + std::to_string(this->denied_total_) + ",\"timeouts\":" + std::to_string(this->status_timeouts_) +
       ",\"send_failures\":" + std::to_string(this->send_failures_) +
       ",\"unknown_sources\":" + std::to_string(this->unknown_sources_.load(std::memory_order_relaxed)) + "}";
  static const char *const IMPORT_STATES[] = {"idle", "queued", "committed", "failed"};
  s += std::string(",\"import\":{\"state\":\"") + IMPORT_STATES[(uint8_t) this->import_state_.load()] + "\"";
  const char *ierr = this->import_error_.load();
  if (ierr != nullptr && ierr[0] != '\0') {
    s += ",\"error\":\"";
    json_escape_into(s, ierr);
    s += "\"";
  }
  s += "},\"fixtures\":[";
  uint32_t now = millis();
  for (uint8_t i = 0; i < this->config_.fixture_count; i++) {
    const Fixture &f = this->config_.fixtures[i];
    if (i)
      s += ",";
    s += "{\"id\":\"" + object_id_for(f) + "\",\"name\":\"";
    json_escape_into(s, f.name);
    s += "\"";
    bool live = this->last_seen_ms_.size() > i && this->last_seen_ms_[i] != 0 && now - this->last_seen_ms_[i] < LIVE_WINDOW_MS;
    s += std::string(",\"live\":") + (live ? "true" : "false");
    if (this->lights_.size() > i && this->lights_[i]->state() != nullptr) {
      const auto &v = this->lights_[i]->state()->current_values;
      s += std::string(",\"heard\":") + (this->lights_[i]->heard() ? "true" : "false");
      s += std::string(",\"on\":") + (v.is_on() ? "true" : "false");
      s += ",\"brightness\":" + std::to_string(wire_intensity_from_brightness(v.get_brightness()));
      if (v.get_color_mode() == light::ColorMode::RGB) {
        uint16_t hue;
        uint8_t sat;
        hsi_from_rgb(v.get_red(), v.get_green(), v.get_blue(), &hue, &sat);
        s += ",\"mode\":\"hsi\",\"hue\":" + std::to_string(hue) + ",\"sat\":" + std::to_string(sat);
      } else {
        s += ",\"mode\":\"cct\",\"kelvin\":" + std::to_string(kelvin_from_mireds(v.get_color_temperature()));
      }
    }
    s += "}";
  }
  s += "]";  // status_json() appends the live PIN fields and closes the object
  std::lock_guard<std::mutex> lock(this->snapshot_mutex_);
  this->snapshot_json_ = std::move(s);
}

std::string AmbleComponent::status_json() {
  std::string s;
  {
    std::lock_guard<std::mutex> lock(this->snapshot_mutex_);
    s = this->snapshot_json_;
  }
  // PIN state is read live (the snapshot is up to 5 s old; a lockout countdown must not be).
  std::lock_guard<std::mutex> lock(this->pin_mutex_);
  s += std::string(",\"pin_set\":") + (this->pin_.is_set() ? "true" : "false");
  s += ",\"locked_ms\":" + std::to_string(this->pin_.locked_for_ms(millis()));
  s += std::string(",\"pin_pending\":") + (this->pin_busy_.load() ? "true" : "false");
  s += ",\"pin_error\":\"";
  json_escape_into(s, this->pin_error_.load());
  s += "\"}";
  return s;
}

}  // namespace amble
}  // namespace esphome

// ---- cloud sign-in (cloud.h) -----------------------------------------------------------------
namespace esphome {
namespace amble {

static const char *const CLOUD_KEY = "cloud";

#ifdef AMBLE_CLOUD_DOOR
static bool load_saved_(NvsBackend &nvs, CloudSaved *out) {
  size_t len = sizeof(*out);
  bool ok = nvs.read(CLOUD_KEY, out, &len) && len == sizeof(*out) && out->magic == CLOUD_SAVED_MAGIC &&
            cloud_region_valid(out->region);
  if (ok) {  // belt and braces: the strings must be terminated
    out->email[CLOUD_EMAIL_MAX] = '\0';
    out->password[CLOUD_PW_MAX] = '\0';
  } else {
    out->wipe();
  }
  return ok;
}
#endif

bool AmbleComponent::cloud_start(const CloudJob &job, const char **error) {
#ifndef AMBLE_CLOUD_DOOR
  (void) job;
  *error = "the cloud sign-in is not in this build";
  return false;
#else
  CloudJob local = job;
  local.email[CLOUD_EMAIL_MAX] = local.password[CLOUD_PW_MAX] = local.region[2] = '\0';
  if (local.use_saved) {
    CloudSaved saved;
    if (!load_saved_(this->nvs_, &saved)) {
      local.wipe();
      *error = "no saved sign-in";
      return false;
    }
    local.wipe();
    strncpy(local.email, saved.email, CLOUD_EMAIL_MAX);
    strncpy(local.password, saved.password, CLOUD_PW_MAX);
    strncpy(local.region, saved.region, 2);
    local.remember = false;
    local.use_saved = true;
    saved.wipe();
  } else if (!cloud_region_valid(local.region) || local.email[0] == '\0' || local.password[0] == '\0') {
    local.wipe();
    *error = "email, password and region are required";
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(this->cloud_mutex_);
    if (this->cloud_state_.phase() == CloudState::Phase::BUSY) {
      local.wipe();
      *error = CLOUD_BUSY;
      return false;
    }
    if (local.use_saved && this->cloud_forget_pending_.load()) {  // forgotten, the erase not yet written
      local.wipe();
      *error = "no saved sign-in";
      return false;
    }
    this->cloud_job_ = local;
    this->cloud_remember_ = local.remember && !local.use_saved;
    this->cloud_state_.start();
    size_t heap_before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    if (xTaskCreate(&AmbleComponent::cloud_worker_, "amble_cloud", 12288, this, 1, nullptr) != pdPASS) {
      this->cloud_job_.wipe();
      CloudOutcome f;
      f.stage = CloudStage::TRANSPORT;
      this->cloud_state_.finish(f);
      ESP_LOGI(TAG, "cloud login failed: stage=%s status=%d code=%d (internal heap %u -> %u)",
               cloud_stage_text(f.stage), f.status, f.code, (unsigned) heap_before,
               (unsigned) heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    }
  }
  local.wipe();
  return true;
#endif
}

void AmbleComponent::cloud_worker_(void *arg) {
  static_cast<AmbleComponent *>(arg)->cloud_run_worker_();
  vTaskDelete(nullptr);
}

void AmbleComponent::cloud_run_worker_() {
#ifdef AMBLE_CLOUD_DOOR
  CloudJob job;
  {
    std::lock_guard<std::mutex> lock(this->cloud_mutex_);
    job = this->cloud_job_;
    this->cloud_job_.wipe();
  }
  size_t heap_before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  CloudOutcome out = cloud_login_call(job);
  size_t heap_after = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  {
    std::lock_guard<std::mutex> lock(this->cloud_mutex_);
    // Decided at this moment, under the lock: a Forget pressed while the sign-in ran has cleared cloud_remember_.
    if (cloud_should_save(out.ok, job.remember && !job.use_saved, !this->cloud_remember_)) {
      this->cloud_pending_save_.wipe();
      this->cloud_pending_save_.magic = CLOUD_SAVED_MAGIC;
      strncpy(this->cloud_pending_save_.email, job.email, CLOUD_EMAIL_MAX);
      strncpy(this->cloud_pending_save_.password, job.password, CLOUD_PW_MAX);
      strncpy(this->cloud_pending_save_.region, job.region, 2);
      this->cloud_save_pending_.store(true);
      this->cloud_store_ = CloudStore::PENDING;
    }
    this->cloud_remember_ = false;
    this->cloud_state_.finish(out);
  }
  job.wipe();
  ESP_LOGI(TAG, "cloud login %s: stage=%s status=%d code=%d (internal heap %u -> %u)", out.ok ? "ok" : "failed",
           cloud_stage_text(out.stage), out.status, out.code, (unsigned) heap_before, (unsigned) heap_after);
  out.token.assign(out.token.size(), '\0');
#endif
}

std::string AmbleComponent::cloud_result_json() {
#ifndef AMBLE_CLOUD_DOOR
  return "{\"state\":\"idle\",\"saved\":null,\"store\":\"ok\"}";
#else
  CloudState::Phase phase;
  CloudOutcome out;
  {
    std::lock_guard<std::mutex> lock(this->cloud_mutex_);
    phase = this->cloud_state_.phase();
    if (phase == CloudState::Phase::DONE || phase == CloudState::Phase::FAILED)
      out = this->cloud_state_.take_result();
  }
  if (phase == CloudState::Phase::IDLE) {
    // A save the main loop has not written yet is still the answer: build the hint from it, not NVS.
    std::string hint, region;
    bool have = false, from_nvs = false;
    CloudStore store;
    {
      std::lock_guard<std::mutex> lock(this->cloud_mutex_);
      bool pending = this->cloud_save_pending_.load() || this->cloud_forget_pending_.load();
      store = pending ? CloudStore::PENDING : this->cloud_store_;
      if (this->cloud_save_pending_.load()) {
        hint = cloud_email_hint(this->cloud_pending_save_.email);
        region = this->cloud_pending_save_.region;
        have = true;
      } else {
        from_nvs = true;
      }
    }
    if (from_nvs) {
      CloudSaved saved;
      if (this->nvs_.probe(CLOUD_KEY) == StoreBackend::Probe::PRESENT && load_saved_(this->nvs_, &saved)) {
        hint = cloud_email_hint(saved.email);
        region = saved.region;
        have = true;
      }
      saved.wipe();
    }
    return cloud_result_text(phase, out, have ? hint.c_str() : nullptr, have ? region.c_str() : nullptr, store);
  }
  std::string s = cloud_result_text(phase, out, nullptr, nullptr, CloudStore::OK);
  out.token.assign(out.token.size(), '\0');
  return s;
#endif
}

bool AmbleComponent::cloud_forget() {
#ifdef AMBLE_CLOUD_DOOR
  std::lock_guard<std::mutex> lock(this->cloud_mutex_);
  this->cloud_remember_ = false;  // a sign-in still running will not be saved
  if (this->cloud_save_pending_.load()) {  // nor one that finished but is not written yet
    this->cloud_pending_save_.wipe();
    this->cloud_save_pending_.store(false);
  }
  this->cloud_forget_pending_.store(true);
  this->cloud_store_ = CloudStore::PENDING;
#endif
  return true;
}

void AmbleComponent::process_pending_cloud_() {
#ifdef AMBLE_CLOUD_DOOR
  if (!this->cloud_save_pending_.load() && !this->cloud_forget_pending_.load())
    return;
  // All under the lock: the pending copy stays visible (to cloud_result_json) until NVS holds the save,
  // and a Forget cannot slip between the check and the write.
  std::lock_guard<std::mutex> lock(this->cloud_mutex_);
  if (this->cloud_save_pending_.load()) {
    CloudSaved save = this->cloud_pending_save_;
    bool ok = this->nvs_.write(CLOUD_KEY, &save, sizeof(save)) && this->nvs_.commit();
    save.wipe();
    this->cloud_pending_save_.wipe();
    this->cloud_save_pending_.store(false);
    this->cloud_store_ = ok ? CloudStore::OK : CloudStore::SAVE_FAILED;
    if (ok)
      ESP_LOGI(TAG, "cloud sign-in saved");
    else
      ESP_LOGW(TAG, "cloud sign-in could not be saved");
  }
  if (this->cloud_forget_pending_.load()) {
    bool ok = this->nvs_.erase(CLOUD_KEY) && this->nvs_.commit();
    this->cloud_forget_pending_.store(false);
    this->cloud_store_ = ok ? CloudStore::OK : CloudStore::FORGET_FAILED;
    if (ok)
      ESP_LOGI(TAG, "cloud sign-in forgotten");
    else
      ESP_LOGW(TAG, "cloud sign-in could not be forgotten");
  }
#endif
}

}  // namespace amble
}  // namespace esphome
