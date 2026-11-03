#pragma once
// Sparse, staggered status polling so Home Assistant tracks changes made outside
// it (the amaran app, a wall switch). Pure and header-only: host-tested.
// Spike 2 finding: the box cannot rely on overhearing the app's traffic.
#include <cstdint>

#include "fixture.h"

namespace esphome {
namespace amble {

class PollScheduler {
 public:
  PollScheduler(uint32_t period_ms, uint32_t stagger_ms) : period_(period_ms), stagger_(stagger_ms) {}
  uint32_t period() const { return this->period_; }
  // Fixture i is first due at now + period + i*stagger, so a fleet never polls in a burst.
  void reset(uint8_t count, uint32_t now_ms) {
    this->count_ = count > MAX_FIXTURES ? MAX_FIXTURES : count;
    for (uint8_t i = 0; i < this->count_; i++)
      this->due_[i] = now_ms + this->period_ + (uint32_t) i * this->stagger_;
  }
  // The most overdue fixture with no request outstanding (pending[i] == 0), or -1.
  // Never within `stagger` of the previous request, so several overdue fixtures
  // (after simultaneous commands, or a long stall) drain spaced out, not in a burst.
  int next_due(uint32_t now_ms, const uint8_t *pending) const {
    if (this->period_ == 0)
      return -1;
    if (this->has_sent_ && (int32_t) (now_ms - this->last_send_ms_) < (int32_t) this->stagger_)
      return -1;
    int best = -1;
    int32_t best_age = 0;
    for (uint8_t i = 0; i < this->count_; i++) {
      if (pending != nullptr && pending[i] != 0)
        continue;
      int32_t age = (int32_t) (now_ms - this->due_[i]);  // wrap-safe
      if (age >= 0 && (best < 0 || age > best_age)) {
        best = i;
        best_age = age;
      }
    }
    return best;
  }
  // A status request went out for i, for any reason: the next poll is a full period away.
  void mark(uint8_t i, uint32_t now_ms) {
    if (i < this->count_)
      this->due_[i] = now_ms + this->period_;
    this->last_send_ms_ = now_ms;
    this->has_sent_ = true;
  }

 protected:
  uint32_t period_;
  uint32_t stagger_;
  uint8_t count_{0};
  uint32_t due_[MAX_FIXTURES]{};
  uint32_t last_send_ms_{0};
  bool has_sent_{false};
};

}  // namespace amble
}  // namespace esphome
