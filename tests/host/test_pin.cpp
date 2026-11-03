#undef MINI_TEST_MAIN
#include "pin.h"
#include "mini_test.h"

using namespace esphome::amble;
using R = PinGate::Result;

TEST(pin_default_is_unset_and_passes) {
  PinGate g; CHECK(!g.is_set());
  CHECK_EQ((int) g.check("", 0), (int) R::OK); CHECK_EQ((int) g.check(nullptr, 0), (int) R::OK);
}
TEST(pin_0000_is_unset) { PinGate g; g.set("0000"); CHECK(!g.is_set()); g.set(""); CHECK(!g.is_set()); }
TEST(pin_format_4_to_8_digits) {
  CHECK(PinGate::valid_format("1234")); CHECK(PinGate::valid_format("12345678")); CHECK(PinGate::valid_format("0000"));
  CHECK(!PinGate::valid_format("123")); CHECK(!PinGate::valid_format("123456789")); CHECK(!PinGate::valid_format("12a4"));
  CHECK(!PinGate::valid_format("")); CHECK(!PinGate::valid_format(nullptr)); CHECK(!PinGate::valid_format("12 4"));
}
TEST(pin_right_and_wrong) {
  PinGate g; g.set("4321"); CHECK(g.is_set());
  CHECK_EQ((int) g.check("4321", 0), (int) R::OK);
  CHECK_EQ((int) g.check("1234", 0), (int) R::WRONG);
  CHECK_EQ((int) g.check("", 0), (int) R::WRONG); CHECK_EQ((int) g.check(nullptr, 0), (int) R::WRONG);
  CHECK_EQ((int) g.check("43210", 0), (int) R::WRONG);  // prefix is not a match
}
TEST(pin_wrong_five_times_locks) {
  PinGate g; g.set("4321");
  for (int i = 0; i < 4; i++) CHECK_EQ((int) g.check("0001", 1000), (int) R::WRONG);
  CHECK_EQ((int) g.check("0001", 1000), (int) R::LOCKED);  // fifth wrong locks
  CHECK(g.locked_for_ms(1000) > 29000); CHECK(g.locked_for_ms(1000) <= 30000);
}
TEST(pin_lockout_holds_correct_pin) {
  PinGate g; g.set("4321");
  for (int i = 0; i < 5; i++) g.check("0001", 1000);
  CHECK_EQ((int) g.check("4321", 20000), (int) R::LOCKED);
  CHECK_EQ((int) g.check("4321", 30999), (int) R::LOCKED);
}
TEST(pin_correct_after_lockout_resets) {
  PinGate g; g.set("4321");
  for (int i = 0; i < 5; i++) g.check("0001", 1000);
  CHECK_EQ((int) g.check("4321", 31000), (int) R::OK);
  CHECK_EQ(g.locked_for_ms(31000), 0u);
  for (int i = 0; i < 4; i++) CHECK_EQ((int) g.check("0001", 31000), (int) R::WRONG);  // counter restarted
}
TEST(pin_correct_resets_failure_count) {
  PinGate g; g.set("4321");
  for (int i = 0; i < 4; i++) g.check("0001", 0);
  CHECK_EQ((int) g.check("4321", 0), (int) R::OK);
  for (int i = 0; i < 4; i++) CHECK_EQ((int) g.check("0001", 0), (int) R::WRONG);
}
TEST(pin_lockout_wraps_millis) {
  PinGate g; g.set("4321");
  for (int i = 0; i < 5; i++) g.check("0001", 0xFFFFF000u);
  CHECK_EQ((int) g.check("4321", 0xFFFFFFF0u), (int) R::LOCKED);
  CHECK_EQ((int) g.check("4321", 30000u), (int) R::OK);
}
