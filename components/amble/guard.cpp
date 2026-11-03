#include "guard.h"

#include "fixture.h"  // is_unicast

namespace esphome {
namespace amble {

void TransmitGuard::set_allowlist(const uint16_t *addrs, size_t n) {
  this->count_ = 0;
  for (size_t i = 0; i < n && this->count_ < MAX_ALLOW; i++)
    if (is_unicast(addrs[i]))
      this->allow_[this->count_++] = addrs[i];
}

void TransmitGuard::set_configured(const uint16_t *addrs, size_t n) {
  this->configured_count_ = 0;
  for (size_t i = 0; i < n && this->configured_count_ < MAX_CONFIGURED; i++)
    if (is_unicast(addrs[i]))
      this->configured_[this->configured_count_++] = addrs[i];
}

Deny TransmitGuard::check(uint16_t dst) const {
  if (dst == 0x0000 || dst == 0x0001)
    return Deny::NOT_UNICAST;  // unassigned, or the amaran app itself
  if (dst >= 0x8000) {
    // virtual, group or broadcast: production only
    return this->allow_group_ ? Deny::ALLOWED : Deny::GROUP_COMPILED_OUT;
  }
  if (this->is_dev()) {
    if (this->count_ == 0)
      return Deny::NO_ALLOWLIST;
    for (size_t i = 0; i < this->count_; i++)
      if (this->allow_[i] == dst)
        return Deny::ALLOWED;
    return Deny::NOT_IN_DEV_ALLOWLIST;
  }
  // release / production: the box's own fixture table, nothing else
  for (size_t i = 0; i < this->configured_count_; i++)
    if (this->configured_[i] == dst)
      return Deny::ALLOWED;
  return Deny::NOT_CONFIGURED;
}

const char *TransmitGuard::text(Deny d) {
  switch (d) {
    case Deny::ALLOWED:
      return "allowed";
    case Deny::NOT_UNICAST:
      return "destination is not a fixture unicast address";
    case Deny::GROUP_COMPILED_OUT:
      return "group/broadcast sends are compiled out of this build";
    case Deny::NOT_IN_DEV_ALLOWLIST:
      return "destination is not in the dev allowlist";
    case Deny::NO_ALLOWLIST:
      return "dev build with an empty allowlist refuses every send";
    case Deny::NOT_CONFIGURED:
      return "destination is not one of this box's configured fixtures";
  }
  return "denied";
}

}  // namespace amble
}  // namespace esphome
