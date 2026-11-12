#include "light.h"

#include <cmath>

namespace esphome {
namespace amble {

uint16_t wire_intensity_from_brightness(float b) {
  if (!(b > 0.0f))
    return 0;
  if (b >= 1.0f)
    return 1000;
  return (uint16_t) lroundf(b * 1000.0f);
}
float brightness_from_wire(uint16_t i) { return (i > 1000 ? 1000 : i) / 1000.0f; }
bool truncated_reply(uint16_t sent, uint16_t reply) { return sent % 10 != 0 && reply == sent - sent % 10; }
uint16_t whole_percent_intensity(uint16_t i) {
  if (i == 0)
    return 0;
  if (i >= 1000)
    return 1000;
  uint16_t r = (uint16_t) ((i + 5) / 10 * 10);
  return r == 0 ? 10 : r;
}
uint16_t kelvin_from_mireds(float mireds) {
  if (!(mireds > 0.0f))
    return 6500;
  return (uint16_t) lroundf(1000000.0f / mireds);
}
float mireds_from_kelvin(uint16_t kelvin) { return kelvin == 0 ? 153.0f : 1000000.0f / kelvin; }

void hsi_from_rgb(float r, float g, float b, uint16_t *hue_deg, uint8_t *sat) {
  float mx = fmaxf(r, fmaxf(g, b)), mn = fminf(r, fminf(g, b));
  float d = mx - mn;
  float h = 0.0f;
  if (d > 1e-6f) {
    if (mx == r)
      h = fmodf((g - b) / d, 6.0f);
    else if (mx == g)
      h = (b - r) / d + 2.0f;
    else
      h = (r - g) / d + 4.0f;
    h *= 60.0f;
    if (h < 0)
      h += 360.0f;
  }
  float s = mx > 1e-6f ? d / mx : 0.0f;
  int hi = (int) lroundf(h);
  if (hi >= 360)
    hi -= 360;
  *hue_deg = (uint16_t) hi;
  *sat = (uint8_t) lroundf(s * 100.0f);
}

void rgb_from_hs(uint16_t hue_deg, uint8_t sat_0_100, float *r, float *g, float *b) {
  float h = (hue_deg % 360) / 60.0f, s = sat_0_100 / 100.0f, v = 1.0f;
  int i = (int) h;
  float f = h - i, p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
  switch (i) {
    case 0: *r = v; *g = t; *b = p; break;
    case 1: *r = q; *g = v; *b = p; break;
    case 2: *r = p; *g = v; *b = t; break;
    case 3: *r = p; *g = q; *b = v; break;
    case 4: *r = t; *g = p; *b = v; break;
    default: *r = v; *g = p; *b = q; break;
  }
}

Look look_from_status(const telink::Status &st) {
  Look l;
  l.hsi = st.is_hs;
  l.intensity = st.intensity > 1000 ? 1000 : st.intensity;
  if (st.is_hs) {
    l.hue = st.hue;
    l.sat = st.sat;
  } else {
    l.kelvin = st.cct_kelvin;
    l.gm = st.gm;
  }
  return l;
}

Look wire_look(const Look &l) {
  Look w = l;
  if (w.intensity > 1000)
    w.intensity = 1000;
  if (w.hsi) {
    if (w.hue > 360)
      w.hue = 360;
    if (w.sat > 100)
      w.sat = 100;
    w.kelvin = 0;
    w.gm = 0;
  } else {
    int t = ((int) w.kelvin + 5) / 10;  // as telink::cct
    t = t < 80 ? 80 : (t > 2000 ? 2000 : t);
    w.kelvin = (uint16_t) (t * 10);
    w.gm = w.gm < -10 ? -10 : (w.gm > 10 ? 10 : w.gm);
    w.hue = 0;
    w.sat = 0;
  }
  return w;
}

bool same_look(const Look &a, const Look &b) {
  if (a.hsi != b.hsi || a.intensity != b.intensity)
    return false;
  return a.hsi ? (a.hue == b.hue && a.sat == b.sat) : (a.kelvin == b.kelvin && a.gm == b.gm);
}

bool look_lost(const Look &sent, const Look &reply, bool have_before, const Look &before) {
  if (same_look(reply, sent))
    return false;
  if (reply.hsi != sent.hsi)
    return true;
  if (reply.intensity != sent.intensity && !truncated_reply(sent.intensity, reply.intensity))
    return true;
  return have_before && same_look(reply, before);
}

SendPlan plan_sends(bool have_ref, const Look &ref, bool fixture_on, bool target_on, const Look &target) {
  SendPlan p;
  bool differs = have_ref && !same_look(ref, target);
  if (target_on) {
    p.power_on = !fixture_on;
    p.look = differs;
  } else {
    p.look = differs;
    p.power_off = true;
  }
  return p;
}

}  // namespace amble
}  // namespace esphome

#if defined(USE_ESP32) && !defined(AMBLE_HOST_TEST)
#include "amble.h"
#include "esphome/core/log.h"

namespace esphome {
namespace amble {

static const char *const TAG = "amble.light";
static const uint32_t REPLY_MIN_MS = 200;

light::LightTraits AmbleLight::get_traits() {
  const Fixture &f = this->parent_->fixture(this->index_);
  light::LightTraits t;
  if (f.caps.color)
    t.set_supported_color_modes({light::ColorMode::COLOR_TEMPERATURE, light::ColorMode::RGB});
  else
    t.set_supported_color_modes({light::ColorMode::COLOR_TEMPERATURE});
  t.set_min_mireds(mireds_from_kelvin(f.caps.cct_max));
  t.set_max_mireds(mireds_from_kelvin(f.caps.cct_min));
  return t;
}

Look AmbleLight::look_from_values_(const light::LightColorValues &v) const {
  const Fixture &f = this->parent_->fixture(this->index_);
  Look l;
  l.intensity = wire_intensity_from_brightness(v.get_brightness());
  if (this->whole_percent_)
    l.intensity = whole_percent_intensity(l.intensity);
  if (v.get_color_mode() == light::ColorMode::RGB) {
    l.hsi = true;
    hsi_from_rgb(v.get_red(), v.get_green(), v.get_blue(), &l.hue, &l.sat);
  } else {
    uint16_t kelvin = kelvin_from_mireds(v.get_color_temperature());
    if (kelvin < f.caps.cct_min)
      kelvin = f.caps.cct_min;
    if (kelvin > f.caps.cct_max)
      kelvin = f.caps.cct_max;
    l.kelvin = kelvin;
    l.gm = this->gm_;
  }
  return wire_look(l);
}

bool AmbleLight::send_look_(const Look &l) {
  uint8_t p[telink::PAYLOAD_LEN];
  if (l.hsi)
    telink::hsi(l.hue, l.sat, l.intensity, p);
  else
    telink::cct(l.kelvin, l.intensity, l.gm, p);
  if (!this->parent_->send(this->index_, p))
    return false;
  if (this->have_ref_ && !same_look(this->ref_, l)) {  // a second try of the same look keeps the first one's
    this->previous_ = this->ref_;
    this->have_previous_ = true;
  }
  this->ref_ = l;
  this->have_ref_ = true;
  this->sent_look_ = l;
  this->sent_ms_ = millis();
  this->await_look_ = true;
  return true;
}

bool AmbleLight::send_power_(bool on) {
  uint8_t p[telink::PAYLOAD_LEN];
  telink::onoff(on, p);
  if (!this->parent_->send(this->index_, p))
    return false;
  this->fixture_on_ = on;
  this->sent_on_ = on;
  this->sent_ms_ = millis();
  this->await_power_ = true;
  return true;
}

bool AmbleLight::realise(const light::LightColorValues &v) {
  Look target = this->look_from_values_(v);
  SendPlan plan = plan_sends(this->have_ref_, this->ref_, this->fixture_on_, v.is_on(), target);
  if (plan.power_on || plan.look || plan.power_off)
    this->resends_left_ = 1;  // a new command from outside: one second try if it is lost
  // The CCT/HSI commands set the look but do not power the fixture up (Spike 2),
  // so power goes first on the way up and last on the way down. Only believe a
  // power state or a look once its send was accepted.
  if (plan.power_on && !this->send_power_(true))
    return false;
  if (plan.look && !this->send_look_(target))
    return false;
  if (plan.power_off && !this->send_power_(false))
    return false;
  return true;
}

void AmbleLight::write_state(light::LightState *state) {
  if (this->in_setup_)
    return;  // ESPHome's boot-time restore, not a command
  if (!this->parent_->mesh_ready()) {
    // A genuine command we cannot carry out yet. Say so, and do not let HA believe
    // it took: put the entity back to the last state the fixture reported.
    ESP_LOGW(TAG, "fixture %u: mesh not ready, command rejected", this->index_);
    this->parent_->publish_last_known(this->index_);
    return;
  }
  if (this->identifying_)
    this->identifying_ = false;  // HA moved the light mid-flash: the new command wins, no restore
  if (!this->realise(state->current_values)) {
    // Refused by the guard or the mesh: do not leave HA believing it took.
    this->parent_->publish_last_known(this->index_);
    return;
  }
  this->parent_->request_status_after(this->index_, 300);
}

void AmbleLight::apply_status(const telink::Status &st) {
  if (this->state_ == nullptr || !st.valid)
    return;
  // The first reply after a command shows what became of it. A reply already in
  // the air when the command went out (the 15 s poll, the read-back of the command
  // before) describes the old state and proves nothing: the box asks for the real
  // one 300 ms after the send, so anything sooner waits.
  bool awaited = (this->await_power_ || this->await_look_) && millis() - this->sent_ms_ >= REPLY_MIN_MS;
  if (awaited) {
    bool check_power = this->await_power_, check_look = this->await_look_;
    this->await_power_ = this->await_look_ = false;
    if (!this->identifying_) {
      Look again = this->sent_look_;
      bool fix = false, lost_look = false;
      if (check_look) {
        if (!this->whole_percent_ && truncated_reply(this->sent_look_.intensity, st.intensity)) {
          // The fixture cut the value to a whole percent. Remember that until the
          // next boot and send the whole percent Home Assistant shows, now and from
          // here on, so the slider never springs back. Fixtures that keep fractions
          // never trip this.
          this->whole_percent_ = true;
          ESP_LOGI(TAG, "fixture %u keeps whole percent only; sending whole percents from now on", this->index_);
          again.intensity = whole_percent_intensity(this->sent_look_.intensity);
          fix = again.intensity != st.intensity;
        } else {
          Look reply = look_from_status(st);
          lost_look = look_lost(this->sent_look_, reply, this->have_reported_, this->reported_) ||
                      look_lost(this->sent_look_, reply, this->have_previous_, this->previous_);
        }
      }
      bool lost_power = check_power && st.on != this->sent_on_;
      if (lost_look || lost_power) {
        // Lost on the air (about one command in a hundred in a burst). One second
        // try; after that the fixture's own report stands.
        if (this->resends_left_ > 0) {
          this->resends_left_--;
          ESP_LOGI(TAG, "fixture %u: %s command did not arrive; sending it again", this->index_,
                   lost_power && lost_look ? "power and look" : (lost_power ? "power" : "look"));
        } else {
          ESP_LOGW(TAG, "fixture %u: command did not arrive on the second try either; showing what the fixture reports",
                   this->index_);
          lost_look = lost_power = false;
        }
      }
      if (fix || lost_look || lost_power) {
        // Do not show Home Assistant the value that is about to be replaced.
        bool on = this->sent_on_;
        this->fixture_on_ = st.on;
        bool sent = false;
        if (lost_power && on)
          sent |= this->send_power_(true);
        if (fix || lost_look)
          sent |= this->send_look_(again);
        if (lost_power && !on)
          sent |= this->send_power_(false);
        if (sent) {
          this->parent_->request_status_after(this->index_, 300);
          return;
        }
      }
    }
  }
  this->fixture_on_ = st.on;
  this->ref_ = look_from_status(st);
  this->have_ref_ = true;
  this->reported_ = this->ref_;
  this->have_reported_ = true;
  if (this->identifying_)
    return;  // the flash is not the state
  light::LightColorValues v;
  v.set_state(st.on);
  v.set_brightness(brightness_from_wire(st.intensity));
  if (st.is_hs) {
    float r, g, b;
    rgb_from_hs(st.hue, st.sat, &r, &g, &b);
    v.set_color_mode(light::ColorMode::RGB);
    v.set_red(r);
    v.set_green(g);
    v.set_blue(b);
    v.set_color_brightness(1.0f);
  } else {
    v.set_color_mode(light::ColorMode::COLOR_TEMPERATURE);
    v.set_color_temperature(mireds_from_kelvin(st.cct_kelvin));
    this->gm_ = st.gm;
  }
  this->last_reported_ = v;
  this->state_->current_values = v;
  this->state_->remote_values = v;
  this->state_->publish_state();
}

void AmbleLight::begin_identify() {
  if (this->state_ == nullptr || !this->parent_->mesh_ready())
    return;
  const Fixture &f = this->parent_->fixture(this->index_);
  this->before_identify_ = this->state_->current_values;
  this->gm_before_identify_ = this->gm_;
  this->identifying_ = true;
  light::LightColorValues flash;
  flash.set_state(true);
  flash.set_brightness(0.5f);
  flash.set_color_mode(light::ColorMode::COLOR_TEMPERATURE);
  uint16_t k = 5600 < f.caps.cct_min ? f.caps.cct_min : (5600 > f.caps.cct_max ? f.caps.cct_max : 5600);
  flash.set_color_temperature(mireds_from_kelvin(k));
  int gm = this->gm_;
  this->gm_ = 0;
  this->realise(flash);  // powers on if needed
  this->gm_ = gm;
}

void AmbleLight::end_identify() {
  if (!this->identifying_)
    return;  // HA took over mid-flash; its command stands
  this->identifying_ = false;
  this->gm_ = this->gm_before_identify_;
  this->realise(this->before_identify_);  // back to what it was, off included
  this->parent_->request_status_after(this->index_, 300);
}

void AmbleGmNumber::control(float value) {
  int gm = (int) lroundf(value);
  this->parent_->set_gm(this->index_, gm);
  this->publish_state(gm);
}

void AmbleIdentifyButton::press_action() { this->parent_->identify(this->index_); }
void AmbleResyncButton::press_action() { this->parent_->resync(); }

}  // namespace amble
}  // namespace esphome
#endif
