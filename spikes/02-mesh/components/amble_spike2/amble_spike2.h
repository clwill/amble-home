#pragma once

#include <cstdint>
#include <mutex>
#include <string>

#include "esphome/core/component.h"

namespace esphome {
namespace amble_spike2 {

// Decoded Telink fixture status (port of the bridge's amaran_status_t).
struct FixtureStatus {
  bool valid{false};
  bool on{false};
  bool is_hs{false};
  uint16_t intensity{0};
  uint16_t cct_kelvin{0};
  int gm{0};
  uint16_t hue{0};
  uint8_t sat{0};
  uint8_t command_type{0};
};

// Spike 2 (throwaway): joins the amaran mesh as its own node with imported keys,
// snoops fixture status replies in userland behind a linker wrap (no IDF patch),
// and — only when `transmit` is enabled — sends Telink vendor commands by unicast
// to one hard-wired target address. Never relays, never beacons, never proxies,
// never sends to a group or broadcast address.
class AmbleSpike2 : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_BLUETOOTH; }

  void set_own_address(uint16_t a) { this->own_address_ = a; }
  void set_transmit(bool t) { this->transmit_ = t; }

  // Commands (REST-driven in the spike). All refuse unless transmit is on and the
  // destination is the configured unicast target.
  bool send_onoff(bool on);
  bool send_brightness(uint16_t intensity_0_1000);
  bool send_cct(uint16_t kelvin, uint16_t intensity_0_1000, int gm);
  bool send_hsi(uint16_t hue, uint8_t sat, uint16_t intensity_0_1000);
  bool send_status_request();

  // Observability for the test harness.
  uint32_t snooped_total() const { return this->snooped_total_; }
  uint32_t snooped_target() const { return this->snooped_target_; }
  uint32_t sent_total() const { return this->sent_total_; }
  uint32_t mesh_seq() const;
  uint32_t mesh_iv_index() const;
  std::string last_status_text() const { std::lock_guard<std::mutex> lock(this->text_mutex_); return this->last_status_text_; }
  std::string boot_note() const { return this->boot_note_; }
  const FixtureStatus &last_target_status() const { return this->last_target_status_; }

  // Called from the wrapped receive path (BTC task). Public for the C shim.
  void on_access_payload(uint16_t src, uint16_t dst, uint32_t seq, const uint8_t *data, uint16_t len);

 protected:
  bool start_mesh_();
  bool self_provision_();
  bool send_telink_(const uint8_t payload[10]);

  uint16_t own_address_{0x00F1};
  bool transmit_{false};
  bool mesh_started_{false};
  bool mesh_failed_{false};
  uint32_t snooped_total_{0};
  uint32_t snooped_target_{0};
  uint32_t sent_total_{0};
  mutable std::mutex text_mutex_;
  std::string last_status_text_{"none"};
  std::string boot_note_{"mesh not started"};
  FixtureStatus last_target_status_{};
};

extern AmbleSpike2 *global_spike2;  // NOLINT

}  // namespace amble_spike2
}  // namespace esphome
