#undef MINI_TEST_MAIN
#include "poll.h"
#include "mini_test.h"

using namespace esphome::amble;

TEST(poll_first_due_is_staggered) {
  PollScheduler p(15000, 150); p.reset(3, 1000);
  uint8_t pend[3] = {0, 0, 0};
  CHECK_EQ(p.next_due(15999, pend), -1);
  CHECK_EQ(p.next_due(16000, pend), 0); p.mark(0, 16000);
  CHECK_EQ(p.next_due(16000, pend), -1);
  CHECK_EQ(p.next_due(16150, pend), 1); p.mark(1, 16150);
  CHECK_EQ(p.next_due(16300, pend), 2); p.mark(2, 16300);
  CHECK_EQ(p.next_due(31000, pend), 0);  // 16000 + period
}
TEST(poll_skips_fixture_with_pending_request) {
  PollScheduler p(15000, 150); p.reset(2, 0);
  uint8_t pend[2] = {1, 0};
  CHECK_EQ(p.next_due(20000, pend), 1);
  pend[1] = 2; CHECK_EQ(p.next_due(20000, pend), -1);
}
TEST(poll_oldest_due_first_and_mark_from_commands) {
  PollScheduler p(15000, 150); p.reset(2, 0);
  uint8_t pend[2] = {0, 0};
  p.mark(0, 10000);  // a command's own status request counts: fixture 0 is now due at 25000
  CHECK_EQ(p.next_due(20000, pend), 1);
  p.mark(1, 20000);
  CHECK_EQ(p.next_due(24999, pend), -1); CHECK_EQ(p.next_due(25000, pend), 0);
}
TEST(poll_zero_period_never_due) {
  PollScheduler p(0, 150); p.reset(2, 0);
  uint8_t pend[2] = {0, 0};
  CHECK_EQ(p.next_due(1000000, pend), -1);
}
TEST(poll_wraps_millis) {
  PollScheduler p(15000, 0); p.reset(1, 0xFFFFF000u);
  uint8_t pend[1] = {0};
  CHECK_EQ(p.next_due(0xFFFFFFF0u, pend), -1);
  CHECK_EQ(p.next_due(12000u, pend), 0);  // 0xFFFFF000 + 15000 wrapped
}

TEST(poll_min_spacing_after_simultaneous_marks) {
  // D4: two fixtures commanded in the same instant become due together; the polls must still be 150 ms apart.
  PollScheduler p(15000, 150); p.reset(2, 0);
  uint8_t pend[2] = {0, 0};
  p.mark(0, 10000); p.mark(1, 10000);
  CHECK_EQ(p.next_due(25000, pend), 0); p.mark(0, 25000);
  CHECK_EQ(p.next_due(25000, pend), -1);
  CHECK_EQ(p.next_due(25149, pend), -1);
  CHECK_EQ(p.next_due(25150, pend), 1); p.mark(1, 25150);
  CHECK_EQ(p.next_due(25150, pend), -1);
}
TEST(poll_overdue_fleet_drains_spaced) {
  PollScheduler p(15000, 150); p.reset(4, 0);
  uint8_t pend[4] = {0, 0, 0, 0};
  uint32_t t = 100000;  // everything long overdue
  int order[4]; int n = 0;
  for (int step = 0; step < 8 && n < 4; step++, t += 75) {
    int d = p.next_due(t, pend);
    if (d >= 0) { order[n++] = d; p.mark((uint8_t) d, t); }
  }
  CHECK_EQ(n, 4);
  CHECK_EQ(order[0], 0); CHECK_EQ(order[1], 1); CHECK_EQ(order[2], 2); CHECK_EQ(order[3], 3);  // oldest first
}
