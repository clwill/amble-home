#include "mesh_node.h"

#include <cstring>

namespace esphome {
namespace amble {

MeshNode *global_mesh_node = nullptr;  // NOLINT

// ---- Freshness (pure) -----------------------------------------------------------
bool Freshness::accept(uint16_t src, uint32_t iv, uint32_t seq) {
  for (size_t i = 0; i < this->count_; i++) {
    Entry &e = this->entries_[i];
    if (e.src != src)
      continue;
    if (iv < e.iv || (iv == e.iv && seq <= e.seq))
      return false;
    e.iv = iv;
    e.seq = seq;
    return true;
  }
  if (this->count_ >= MAX_SOURCES)
    return false;
  this->entries_[this->count_++] = {src, iv, seq};
  return true;
}

void Freshness::reset() { this->count_ = 0; }

}  // namespace amble
}  // namespace esphome

#if defined(USE_ESP32) && !defined(AMBLE_HOST_TEST)
// ---- IDF glue ----------------------------------------------------------------------
#include "esp_random.h"
#include "esphome/core/log.h"

extern "C" {
#include "esp_ble_mesh_common_api.h"
#include "esp_ble_mesh_config_model_api.h"
#include "esp_ble_mesh_defs.h"
#include "esp_ble_mesh_networking_api.h"
#include "esp_ble_mesh_provisioning_api.h"
// Internal mesh headers (exposed to project sources by ESPHome's native build):
// self-provisioning with known keys, key injection, the key material and the
// crypto primitives the snoop uses. See docs/decisions.md.
#include "access.h"
#include "crypto.h"
#include "local.h"
#include "mesh/main.h"
#include "net.h"
#include "storage/settings.h"
}

#include "telink.h"

namespace esphome {
namespace amble {

static const char *const TAG = "amble.mesh";

// Receive-only posture apart from our own unicasts: no relay, no beacons, no
// GATT proxy. Range through relaying is a milestone-3 question.
static esp_ble_mesh_cfg_srv_t config_server = {
    .net_transmit = ESP_BLE_MESH_TRANSMIT(2, 20),
    .relay = ESP_BLE_MESH_RELAY_DISABLED,
    .relay_retransmit = ESP_BLE_MESH_TRANSMIT(2, 20),
    .beacon = ESP_BLE_MESH_BEACON_DISABLED,
    .gatt_proxy = ESP_BLE_MESH_GATT_PROXY_DISABLED,
    .default_ttl = 7,
};
static esp_ble_mesh_model_op_t vnd_op[] = {
    ESP_BLE_MESH_MODEL_OP(ESP_BLE_MESH_MODEL_OP_3(telink::OPCODE, telink::COMPANY_ID), 1),
    ESP_BLE_MESH_MODEL_OP(telink::OPCODE, 1),
    ESP_BLE_MESH_MODEL_OP_END,
};
static esp_ble_mesh_model_t root_models[] = {ESP_BLE_MESH_MODEL_CFG_SRV(&config_server)};
static esp_ble_mesh_model_t vnd_models[] = {
    ESP_BLE_MESH_VENDOR_MODEL(telink::COMPANY_ID, telink::VENDOR_MODEL_ID, vnd_op, NULL, NULL)};
static esp_ble_mesh_elem_t elements[] = {ESP_BLE_MESH_ELEMENT(0, root_models, vnd_models)};
static esp_ble_mesh_comp_t composition = {.cid = telink::COMPANY_ID, .element_count = 1, .elements = elements};
static uint8_t dev_uuid[16] = {'a', 'm', 'b', 'l', 'e', '-', 'h', 'o', 'm', 'e', 0, 0, 0, 0, 0, 0};
static esp_ble_mesh_prov_t provision = {.uuid = dev_uuid};

static void prov_cb(esp_ble_mesh_prov_cb_event_t event, esp_ble_mesh_prov_cb_param_t *param) {
  if (event == ESP_BLE_MESH_PROV_REGISTER_COMP_EVT)
    ESP_LOGD(TAG, "stack registered (err %d)", param->prov_register_comp.err_code);
  else if (event == ESP_BLE_MESH_NODE_PROV_COMPLETE_EVT)
    ESP_LOGD(TAG, "node provisioned (net_idx %u)", param->node_prov_complete.net_idx);
}
static void model_cb(esp_ble_mesh_model_cb_event_t event, esp_ble_mesh_model_cb_param_t *param) {
  if (event == ESP_BLE_MESH_MODEL_OPERATION_EVT)
    ESP_LOGD(TAG, "message addressed to us: op 0x%06lx len %u", (unsigned long) param->model_operation.opcode,
             param->model_operation.length);
}

bool MeshNode::start(const Config &cfg) {
  if (cfg.own_address == 0) {
    this->failure_text_ = "the box has no mesh address (its address seed could not be read or stored)";
    this->failed_ = true;
    ESP_LOGE(TAG, "%s", this->failure_text_);
    return false;
  }
  if (this->started_ || this->failed_)
    return this->started_;
  global_mesh_node = this;
  esp_err_t err = esp_ble_mesh_register_prov_callback(prov_cb);
  if (err == ESP_OK)
    err = esp_ble_mesh_register_custom_model_callback(model_cb);
  if (err == ESP_OK)
    err = esp_ble_mesh_init(&provision, &composition);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "mesh init failed: %s", esp_err_to_name(err));
    this->failure_text_ = "mesh stack initialisation failed";
    this->failed_ = true;
    return false;
  }
  if (!this->self_provision_(cfg)) {
    this->failed_ = true;
    if (this->failure_text_[0] == '\0')
      this->failure_text_ = "mesh provisioning failed";
    return false;
  }
  this->started_ = true;
  return true;
}

// Spike 2 sequence. IDF 5.5 split bt_mesh_provision() into pre_provision() +
// provision(). On later boots the stack restores the node from NVS
// (CONFIG_BLE_MESH_SETTINGS) including the sequence number, which it stores on
// every increment (CONFIG_BLE_MESH_SEQ_STORE_RATE 0) — a reboot cannot replay.
bool MeshNode::self_provision_(const Config &cfg) {
  // The identity history (identity.h) keeps the sequence number and IV index
  // reached under every (network, address) this box has used, so leaving an
  // identity and coming back never restarts a sequence the fixtures remember.
  if (this->store_ == nullptr) {
    this->failure_text_ = "no store for the mesh safety record";
    ESP_LOGE(TAG, "%s", this->failure_text_);
    return false;
  }
  if (this->history_.load(this->store_) == IdentityHistory::Load::UNREADABLE) {
    // Stored but untrustworthy, or the store could not say: treating either as
    // empty could restart a sequence. (An erase gives the box a new address, so it
    // is a safe way out; see amble.cpp's address seed.)
    this->failure_text_ = "the mesh safety record on the device is unreadable; erase the device from the flasher";
    ESP_LOGE(TAG, "%s", this->failure_text_);
    return false;
  }
  uint8_t want_net_id[8];
  bt_mesh_k3(cfg.net_key, want_net_id);  // the network's public id, not the key

  bool restored = bt_mesh_is_provisioned();
  if (restored) {
    // The stack restored a node from NVS. It is only reused if it is THIS
    // configuration's node: same address and NetKey. Otherwise its safety state
    // is recorded, the stored identity discarded, and the node provisions afresh
    // below (#25). Running on the wrong identity would desynchronise the fixture
    // table from the mesh in use.
    struct bt_mesh_subnet *sub = bt_mesh_rx_netkey_size() ? bt_mesh_rx_netkey_get(0) : nullptr;
    bool same_addr = bt_mesh_primary_addr() == cfg.own_address;
    bool same_net = sub != nullptr && memcmp(sub->keys[0].net, cfg.net_key, 16) == 0;
    // A switch interrupted between the stack's own writes can restore the right
    // identity with an IV index below what this network reached: handled like a
    // mismatch, so the fresh path below re-provisions at the recorded IV (D1.3).
    bool iv_low = same_addr && same_net && bt_mesh.iv_index < this->history_.iv_for(want_net_id);
    if (!same_addr || !same_net || iv_low) {
      ESP_LOGW(TAG, "mesh identity stored on the device differs from the configuration (%s); re-provisioning",
               iv_low ? "IV index below the record" : same_net ? "address" : "network");
      uint8_t had_net_id[8] = {};
      if (sub != nullptr)
        bt_mesh_k3(sub->keys[0].net, had_net_id);
      if (!this->history_.remember(had_net_id, bt_mesh_primary_addr(), bt_mesh.seq, bt_mesh.iv_index)) {
        this->failure_text_ = "this box has used too many mesh identities to switch again; erase the device from the flasher";
        ESP_LOGE(TAG, "%s", this->failure_text_);
        return false;
      }
      if (!this->history_.save(this->store_)) {
        // Fail closed: without the record, returning to this identity later could replay.
        this->failure_text_ = "could not record the mesh safety state; identity left unchanged";
        ESP_LOGE(TAG, "%s", this->failure_text_);
        return false;
      }
      bt_mesh_node_reset();  // synchronous: clears the stored node, keys, sequence and IV state
      if (bt_mesh_is_provisioned()) {
        this->failure_text_ = "could not clear the mesh identity stored on the device";
        ESP_LOGE(TAG, "%s", this->failure_text_);
        return false;
      }
      restored = false;
    }
  }
  if (restored) {
    // A switch interrupted between the stack persisting the new node and the
    // resumed sequence number being persisted restores with a low sequence: the
    // history's floor applies on this path too, before anything is sent.
    uint32_t floor = 0;
    if (this->history_.resume_seq(want_net_id, cfg.own_address, &floor) && bt_mesh.seq < floor) {
      ESP_LOGW(TAG, "restored sequence %lu is below the record for this identity; resuming at %lu",
               (unsigned long) bt_mesh.seq, (unsigned long) floor);
      bt_mesh.seq = floor;
      bt_mesh_store_seq();
    }
    this->restored_ = true;
    ESP_LOGI(TAG, "mesh node restored from NVS (seq %lu, iv %lu)", (unsigned long) bt_mesh.seq,
             (unsigned long) bt_mesh.iv_index);
  } else {
    esp_err_t err = esp_ble_mesh_node_prov_enable(
        (esp_ble_mesh_prov_bearer_t) (ESP_BLE_MESH_PROV_ADV | ESP_BLE_MESH_PROV_GATT));
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "node_prov_enable: %s", esp_err_to_name(err));
      return false;
    }
    uint8_t dev_key[16];
    esp_fill_random(dev_key, sizeof(dev_key));  // only for Config traffic we never do
    // An identity used before resumes its sequence number past where it got to;
    // a network seen before (under any address) starts at the IV index it reached.
    uint32_t iv = this->history_.iv_for(want_net_id);
    uint32_t resume = 0;
    bool resumed = this->history_.resume_seq(want_net_id, cfg.own_address, &resume);
    int rc = bt_mesh_pre_provision(cfg.net_key, 0, 0, iv, cfg.own_address, dev_key);
    if (rc != 0) {
      ESP_LOGE(TAG, "bt_mesh_pre_provision rc=%d", rc);
      return false;
    }
    bt_mesh_provision();
    if (resumed) {
      bt_mesh.seq = resume;
      bt_mesh_store_seq();
    }
    esp_ble_mesh_node_prov_disable((esp_ble_mesh_prov_bearer_t) (ESP_BLE_MESH_PROV_ADV | ESP_BLE_MESH_PROV_GATT));
    ESP_LOGI(TAG, "mesh node self-provisioned (%s; seq %lu, iv %lu)", resumed ? "identity used before" : "fresh",
             (unsigned long) bt_mesh.seq, (unsigned long) bt_mesh.iv_index);
  }
  // IDF 5.5: add returns -EEXIST when the index is taken (it does not compare the
  // bytes, so verify them ourselves); bind returns -EALREADY when already bound.
  int rc = bt_mesh_node_local_app_key_add(0, 0, cfg.app_key);
  if (rc != 0 && rc != -EEXIST) {
    this->failure_text_ = "could not install the application key";
    ESP_LOGE(TAG, "app key add rc=%d", rc);
    return false;
  }
  struct bt_mesh_app_key *key = bt_mesh_rx_appkey_size() ? bt_mesh_rx_appkey_get(0) : nullptr;
  if (key == nullptr || memcmp(key->keys[0].val, cfg.app_key, 16) != 0) {
    this->failure_text_ = "application key stored on the device does not match the active configuration";
    ESP_LOGE(TAG, "%s", this->failure_text_);
    return false;
  }
  rc = bt_mesh_node_bind_app_key_to_model(bt_mesh_primary_addr(), telink::VENDOR_MODEL_ID, telink::COMPANY_ID, 0);
  if (rc != 0 && rc != -EALREADY && rc != -EEXIST) {
    this->failure_text_ = "could not bind the application key to the vendor model";
    ESP_LOGE(TAG, "app key bind rc=%d", rc);
    return false;
  }
  return true;
}

bool MeshNode::send_vendor(uint16_t dst, const uint8_t payload[10]) {
  if (!this->started_)
    return false;
  esp_ble_mesh_msg_ctx_t ctx = {};
  ctx.net_idx = 0;
  ctx.app_idx = 0;
  ctx.addr = dst;
  ctx.send_ttl = 7;
  esp_err_t err = esp_ble_mesh_server_model_send_msg(&vnd_models[0], &ctx, telink::OPCODE, 10, (uint8_t *) payload);
  if (err != ESP_OK)
    ESP_LOGW(TAG, "send failed: %s", esp_err_to_name(err));
  return err == ESP_OK;
}

uint32_t MeshNode::seq() const { return this->started_ ? bt_mesh.seq : 0; }
uint32_t MeshNode::iv_index() const { return this->started_ ? bt_mesh.iv_index : 0; }
uint16_t MeshNode::own_address() const { return this->started_ ? bt_mesh_primary_addr() : 0; }

}  // namespace amble
}  // namespace esphome

// ---- the linker-wrap snoop -----------------------------------------------------------
// Every bearer reaches bt_mesh_generic_net_recv from another object file, so --wrap
// catches them all. The real function runs first and untouched; then a COPY of the
// raw network PDU is decoded here with the IDF's exported primitives and the keys
// the stack holds, mirroring net_decrypt()/flooding_decrypt() without touching the
// stack's message cache. Fixtures reply to the app (0x0001), never to us, which is
// why this exists (docs/origin.md item 4).
using esphome::amble::AccessMessage;
using esphome::amble::global_mesh_node;

extern "C" void __real_bt_mesh_generic_net_recv(struct net_buf_simple *data, struct bt_mesh_net_rx *rx,
                                                enum bt_mesh_net_if net_if);

namespace {

constexpr size_t MAX_PDU = 32;  // adv-bearer network PDU is at most 29 bytes
constexpr size_t MIN_PDU = BLE_MESH_NET_HDR_LEN + 1 + 4;

void try_app_decrypt(struct bt_mesh_subnet *sub, bool new_key, struct net_buf_simple *net, uint32_t iv) {
  if (net->len < BLE_MESH_NET_HDR_LEN + 1 + 1 + 4)
    return;
  if (BLE_MESH_NET_HDR_CTL(net->data))
    return;  // control message
  uint32_t seq = BLE_MESH_NET_HDR_SEQ(net->data);
  uint16_t src = BLE_MESH_NET_HDR_SRC(net->data);
  uint16_t dst = BLE_MESH_NET_HDR_DST(net->data);
  if (global_mesh_node == nullptr || !global_mesh_node->wanted(src))
    return;  // not one of our fixtures: never consumes a freshness slot
  uint8_t thdr = net->data[BLE_MESH_NET_HDR_LEN];
  if (thdr & 0x80)
    return;  // segmented: status replies are not
  if (!(thdr & 0x40))
    return;  // AKF=0: device-key traffic
  uint8_t aid = thdr & 0x3f;
  const uint8_t *tp = net->data + BLE_MESH_NET_HDR_LEN + 1;
  uint16_t tlen = net->len - BLE_MESH_NET_HDR_LEN - 1;

  size_t count = bt_mesh_rx_appkey_size();
  for (size_t i = 0; i < count; i++) {
    struct bt_mesh_app_key *key = bt_mesh_rx_appkey_get(i);
    if (!key || key->net_idx != sub->net_idx)
      continue;
    auto *keys = (new_key && key->updated) ? &key->keys[1] : &key->keys[0];
    if (aid != keys->id)
      continue;
    struct net_buf_simple in;
    net_buf_simple_init_with_data(&in, (void *) tp, tlen);
    in.len -= 4;  // TransMIC
    uint8_t storage[MAX_PDU];
    struct net_buf_simple out;
    net_buf_simple_init_with_data(&out, storage, sizeof(storage));
    out.len = 0;
    if (bt_mesh_app_decrypt(keys->val, false, 0, &in, &out, NULL, src, dst, seq, iv) != 0)
      continue;  // another key may share this AID
    if (global_mesh_node != nullptr && global_mesh_node->freshness().accept(src, iv, seq))
      global_mesh_node->deliver(AccessMessage{src, dst, seq, out.data, out.len});
    return;
  }
}

void snoop(const uint8_t *raw, size_t len) {
  if (len < MIN_PDU || len > MAX_PDU)
    return;
  bool old_iv = BLE_MESH_NET_HDR_IVI(raw) != (bt_mesh.iv_index & 0x01);
  uint32_t iv = bt_mesh.iv_index - (old_iv ? 1 : 0);
  size_t subs = bt_mesh_rx_netkey_size();
  for (size_t i = 0; i < subs; i++) {
    struct bt_mesh_subnet *sub = bt_mesh_rx_netkey_get(i);
    if (!sub || sub->net_idx == BLE_MESH_KEY_UNUSED)
      continue;
    int nkeys = (sub->kr_phase == BLE_MESH_KR_NORMAL) ? 1 : 2;
    for (int k = 0; k < nkeys; k++) {
      if (BLE_MESH_NET_HDR_NID(raw) != sub->keys[k].nid)
        continue;
      uint8_t storage[MAX_PDU];
      struct net_buf_simple buf;
      net_buf_simple_init_with_data(&buf, storage, sizeof(storage));
      buf.len = 0;
      memcpy(net_buf_simple_add(&buf, len), raw, len);
      if (bt_mesh_net_obfuscate(buf.data, iv, sub->keys[k].privacy))
        continue;
      if (!esphome::amble::is_unicast(BLE_MESH_NET_HDR_SRC(buf.data)))
        continue;
      if (bt_mesh_net_decrypt(sub->keys[k].enc, &buf, iv, false, false) != 0)
        continue;
      try_app_decrypt(sub, k == 1, &buf, iv);
      return;
    }
  }
}

}  // namespace

extern "C" void __wrap_bt_mesh_generic_net_recv(struct net_buf_simple *data, struct bt_mesh_net_rx *rx,
                                                enum bt_mesh_net_if net_if) {
  uint8_t raw[MAX_PDU];
  size_t len = data->len <= MAX_PDU ? data->len : 0;
  if (len)
    memcpy(raw, data->data, len);
  __real_bt_mesh_generic_net_recv(data, rx, net_if);
  if (len && net_if == BLE_MESH_NET_IF_ADV && global_mesh_node != nullptr && global_mesh_node->started())
    snoop(raw, len);
}
#endif  // USE_ESP32 && !AMBLE_HOST_TEST
