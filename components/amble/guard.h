#pragma once
// The transmit guard (spec "Dev/production guard"). Pure. Three modes at the
// single transmit boundary:
//   dev (default)       fail-closed allowlist of unicast addresses (the spare);
//                       group/broadcast compiled out.
//   release             any address in the box's own fixture table; group/broadcast
//                       compiled out. What strangers flash.
//   production          release semantics plus group/broadcast (allow_group).
//                       Chris's studio config only.
#include <cstddef>
#include <cstdint>

namespace esphome {
namespace amble {

enum class Deny : uint8_t {
  ALLOWED,
  NOT_UNICAST,
  GROUP_COMPILED_OUT,
  NOT_IN_DEV_ALLOWLIST,
  NO_ALLOWLIST,
  NOT_CONFIGURED,  // release/production: the address is not in the box's fixture table
};

class TransmitGuard {
 public:
  TransmitGuard(bool allow_group, bool release) : allow_group_(allow_group), release_(release || allow_group) {}
  // Dev builds: only these unicast addresses may receive anything. Empty = refuse all.
  void set_allowlist(const uint16_t *addrs, size_t n);
  // Release/production builds: the box's own fixture table. Empty = refuse all.
  void set_configured(const uint16_t *addrs, size_t n);
  Deny check(uint16_t dst) const;
  bool is_dev() const { return !this->release_; }
  const char *mode() const { return this->allow_group_ ? "production" : (this->release_ ? "release" : "dev"); }
  size_t allowlist_size() const { return this->count_; }
  size_t configured_size() const { return this->configured_count_; }
  static const char *text(Deny d);

 protected:
  static constexpr size_t MAX_ALLOW = 8;
  static constexpr size_t MAX_CONFIGURED = 32;  // MAX_FIXTURES
  bool allow_group_;
  bool release_;
  uint16_t allow_[MAX_ALLOW]{};
  size_t count_{0};
  uint16_t configured_[MAX_CONFIGURED]{};
  size_t configured_count_{0};
};

}  // namespace amble
}  // namespace esphome
