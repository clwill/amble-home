#pragma once
// One Home Assistant light per fixture, created at boot from the NVS table.
// The pure conversions are host-tested; the ESPHome classes compile on device.
#include <cstdint>

#include "fixture.h"
#include "telink.h"

namespace esphome {
namespace amble {

// Wire ↔ HA conversions (spec "Light semantics": 0–1000 on the wire, Kelvin
// within the fixture's range, HSI when colour is set).
uint16_t wire_intensity_from_brightness(float b_0_1);
float brightness_from_wire(uint16_t i_0_1000);
uint16_t kelvin_from_mireds(float mireds);
float mireds_from_kelvin(uint16_t kelvin);
void hsi_from_rgb(float r, float g, float b, uint16_t *hue_deg, uint8_t *sat_0_100);
void rgb_from_hs(uint16_t hue_deg, uint8_t sat_0_100, float *r, float *g, float *b);

// A fixture's look: everything but power. The fixture keeps it while off and
// reports it (bench, 2026-10-26), so power and look are commanded separately.
struct Look {
  bool hsi{false};
  uint16_t intensity{0};  // 0..1000
  uint16_t kelvin{0};     // CCT mode
  int gm{0};              // CCT mode
  uint16_t hue{0};        // HSI mode
  uint8_t sat{0};         // HSI mode
};
Look look_from_status(const telink::Status &st);
bool same_look(const Look &a, const Look &b);

struct SendPlan {
  bool power_on{false};
  bool look{false};
  bool power_off{false};
};
// Which commands realise `target`, given the reference look (the last status
// report or accepted look command) and the last known power state. A bare
// turn-on sends power only; the look goes out only when it differs from the
// reference. With no reference (fixture never heard) only power is sent and the
// status reply tells HA what the fixture did. A turn-off re-sends a differing
// look first (identify restore) and then power off. Identical state: nothing.
SendPlan plan_sends(bool have_ref, const Look &ref, bool fixture_on, bool target_on, const Look &target);

}  // namespace amble
}  // namespace esphome

#if defined(USE_ESP32) && !defined(AMBLE_HOST_TEST)
#include "esphome/components/button/button.h"
#include "esphome/components/light/light_output.h"
#include "esphome/components/light/light_state.h"
#include "esphome/components/number/number.h"
#include "esphome/core/component.h"

namespace esphome {
namespace amble {

class AmbleComponent;

class AmbleLight : public light::LightOutput {
 public:
  AmbleLight(AmbleComponent *parent, uint8_t index) : parent_(parent), index_(index) {}
  light::LightTraits get_traits() override;
  void write_state(light::LightState *state) override;
  void set_state(light::LightState *s) { this->state_ = s; }
  light::LightState *state() const { return this->state_; }
  uint8_t index() const { return this->index_; }

  // Snooped status reply (main loop): reflect without re-sending. Ignored while
  // an Identify flash is in progress so the flash never becomes "the state".
  void apply_status(const telink::Status &st);
  // Sends the commands that realise `v` on the fixture (power-on first if needed).
  // Returns true if every send was accepted by the mesh node.
  bool realise(const light::LightColorValues &v);
  // Identify: 50 % white for `hold_ms`, then back to the state before the flash,
  // unless HA changed it meanwhile.
  void begin_identify();
  void end_identify();
  bool identifying() const { return this->identifying_; }

  int gm() const { return this->gm_; }
  void set_gm(int gm) { this->gm_ = gm; }
  bool fixture_on() const { return this->fixture_on_; }
  // What the fixture last reported (off until it has spoken). Rollback target for rejected commands.
  const light::LightColorValues &last_reported() const { return this->last_reported_; }
  bool heard() const { return this->have_ref_; }
  // Suppress the write ESPHome issues from LightState::setup() (restore) — it is
  // not a user command and must never reach the mesh.
  void set_in_setup(bool v) { this->in_setup_ = v; }

 protected:
  AmbleComponent *parent_;
  uint8_t index_;
  light::LightState *state_{nullptr};
  int gm_{0};
  bool fixture_on_{false};  // last power state the fixture reported, or we successfully commanded
  bool in_setup_{false};
  bool identifying_{false};
  light::LightColorValues before_identify_{};
  light::LightColorValues last_reported_{};
  int gm_before_identify_{0};
  // Reference look for plan_sends: the last status report, or the last accepted look command.
  Look ref_{};
  bool have_ref_{false};
  Look look_from_values_(const light::LightColorValues &v) const;
};

class AmbleGmNumber : public number::Number {
 public:
  AmbleGmNumber(AmbleComponent *parent, uint8_t index) : parent_(parent), index_(index) {}
  void control(float value) override;

 protected:
  AmbleComponent *parent_;
  uint8_t index_;
};

class AmbleIdentifyButton : public button::Button {
 public:
  AmbleIdentifyButton(AmbleComponent *parent, uint8_t index) : parent_(parent), index_(index) {}
  void press_action() override;

 protected:
  AmbleComponent *parent_;
  uint8_t index_;
};

class AmbleResyncButton : public button::Button {
 public:
  explicit AmbleResyncButton(AmbleComponent *parent) : parent_(parent) {}
  void press_action() override;

 protected:
  AmbleComponent *parent_;
};

}  // namespace amble
}  // namespace esphome
#endif
