#pragma once
// The Edit-page PIN (spec "Access to the setup page"): a casual-access lock for
// a shared LAN, gating the mutating HTTP routes. "0000" or empty means no gate.
// Pure and host-tested; the component stores the PIN in NVS and feeds millis().
#include <cstdint>

namespace esphome {
namespace amble {

class PinGate {
 public:
  enum class Result : uint8_t { OK, WRONG, LOCKED };
  static constexpr uint8_t MAX_FAILURES = 5;
  static constexpr uint32_t LOCKOUT_MS = 30000;
  static constexpr uint8_t MAX_LEN = 8;

  static bool valid_format(const char *pin);  // 4..8 ASCII digits
  void set(const char *pin);                  // invalid, empty or "0000" clears the gate
  bool is_set() const { return this->pin_[0] != '\0'; }
  const char *pin() const { return this->pin_; }
  Result check(const char *candidate, uint32_t now_ms);
  uint32_t locked_for_ms(uint32_t now_ms) const;

 protected:
  char pin_[MAX_LEN + 1]{};
  uint8_t failures_{0};
  bool locked_{false};
  uint32_t locked_until_{0};
};

}  // namespace amble
}  // namespace esphome
