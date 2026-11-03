#include "pin.h"

#include <cstring>

namespace esphome {
namespace amble {

bool PinGate::valid_format(const char *pin) {
  if (pin == nullptr)
    return false;
  size_t n = strlen(pin);
  if (n < 4 || n > MAX_LEN)
    return false;
  for (size_t i = 0; i < n; i++)
    if (pin[i] < '0' || pin[i] > '9')
      return false;
  return true;
}

void PinGate::set(const char *pin) {
  this->failures_ = 0;
  this->locked_ = false;
  if (!valid_format(pin) || strcmp(pin, "0000") == 0) {
    this->pin_[0] = '\0';
    return;
  }
  strncpy(this->pin_, pin, MAX_LEN);
  this->pin_[MAX_LEN] = '\0';
}

uint32_t PinGate::locked_for_ms(uint32_t now_ms) const {
  if (!this->locked_)
    return 0;
  int32_t left = (int32_t) (this->locked_until_ - now_ms);  // wrap-safe
  return left > 0 ? (uint32_t) left : 0;
}

PinGate::Result PinGate::check(const char *candidate, uint32_t now_ms) {
  if (!this->is_set())
    return Result::OK;
  if (this->locked_) {
    if (this->locked_for_ms(now_ms) > 0)
      return Result::LOCKED;  // a correct PIN does not shorten the lockout
    this->locked_ = false;
    this->failures_ = 0;
  }
  // Constant-time over the stored length; a nullptr or empty candidate is just wrong.
  size_t n = strlen(this->pin_);
  const char *c = candidate == nullptr ? "" : candidate;
  size_t cn = strlen(c);
  uint8_t diff = (uint8_t) (cn != n);
  for (size_t i = 0; i < n; i++)
    diff |= (uint8_t) (this->pin_[i] ^ (i < cn ? c[i] : 0));
  if (diff == 0) {
    this->failures_ = 0;
    return Result::OK;
  }
  if (++this->failures_ >= MAX_FAILURES) {
    this->failures_ = 0;
    this->locked_ = true;
    this->locked_until_ = now_ms + LOCKOUT_MS;
    return Result::LOCKED;
  }
  return Result::WRONG;
}

}  // namespace amble
}  // namespace esphome
