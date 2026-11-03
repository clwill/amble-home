#undef MINI_TEST_MAIN
#include <string>
#include "guard.h"
#include "mini_test.h"

using namespace esphome::amble;

TEST(guard_dev_allows_only_listed) {
  TransmitGuard g(false, false); uint16_t a[] = {0x001E}; g.set_allowlist(a, 1);
  CHECK_EQ((int) g.check(0x001E), (int) Deny::ALLOWED);
  CHECK_EQ((int) g.check(0x000C), (int) Deny::NOT_IN_DEV_ALLOWLIST);
  CHECK_EQ((int) g.check(0x00F0), (int) Deny::NOT_IN_DEV_ALLOWLIST);
}
TEST(guard_dev_empty_allowlist_refuses_everything) {
  TransmitGuard g(false, false);
  CHECK_EQ((int) g.check(0x001E), (int) Deny::NO_ALLOWLIST);
  CHECK_EQ((int) g.check(0x000C), (int) Deny::NO_ALLOWLIST);
  CHECK(g.is_dev()); CHECK_EQ(g.allowlist_size(), 0u);
}
TEST(guard_refuses_group_and_broadcast_when_compiled_out) {
  TransmitGuard g(false, false); uint16_t a[] = {0x001E, 0xC000, 0xFFFF}; g.set_allowlist(a, 3);
  CHECK_EQ(g.allowlist_size(), 1u);  // non-unicast entries are dropped from the allowlist
  CHECK_EQ((int) g.check(0xC000), (int) Deny::GROUP_COMPILED_OUT);
  CHECK_EQ((int) g.check(0xFFFF), (int) Deny::GROUP_COMPILED_OUT);
  CHECK_EQ((int) g.check(0x8000), (int) Deny::GROUP_COMPILED_OUT);
}
TEST(guard_production_allows_group_and_configured_unicast) {
  TransmitGuard g(true, true); uint16_t t[] = {0x000C}; g.set_configured(t, 1);
  CHECK_EQ((int) g.check(0xC000), (int) Deny::ALLOWED);
  CHECK_EQ((int) g.check(0xFFFF), (int) Deny::ALLOWED);
  CHECK_EQ((int) g.check(0x000C), (int) Deny::ALLOWED);
  CHECK_EQ((int) g.check(0x000D), (int) Deny::NOT_CONFIGURED);  // milestone 4: production is release + group, not "any unicast"
  CHECK(!g.is_dev());
}
TEST(guard_never_allows_app_or_unassigned) {
  TransmitGuard dev(false, false); uint16_t a[] = {0x0001}; dev.set_allowlist(a, 1);
  TransmitGuard prod(true, true);
  CHECK_EQ((int) dev.check(0x0000), (int) Deny::NOT_UNICAST); CHECK_EQ((int) dev.check(0x0001), (int) Deny::NOT_UNICAST);
  CHECK_EQ((int) prod.check(0x0000), (int) Deny::NOT_UNICAST); CHECK_EQ((int) prod.check(0x0001), (int) Deny::NOT_UNICAST);
  CHECK_EQ(dev.allowlist_size(), 0u);
}
TEST(guard_text_is_fixed) { CHECK(TransmitGuard::text(Deny::NO_ALLOWLIST)[0] == 'd'); }

// ---- release and production modes (milestone 4) --------------------------------------
TEST(release_allows_only_configured) {
  TransmitGuard g(false, true); uint16_t t[] = {0x001E, 0x000C}; g.set_configured(t, 2);
  CHECK(!g.is_dev()); CHECK_EQ(g.configured_size(), 2u);
  CHECK_EQ((int) g.check(0x001E), (int) Deny::ALLOWED); CHECK_EQ((int) g.check(0x000C), (int) Deny::ALLOWED);
  CHECK_EQ((int) g.check(0x0010), (int) Deny::NOT_CONFIGURED); CHECK_EQ((int) g.check(0x00F0), (int) Deny::NOT_CONFIGURED);
  CHECK_EQ((int) g.check(0x0001), (int) Deny::NOT_UNICAST);
}
TEST(release_refuses_when_nothing_configured) {
  TransmitGuard g(false, true);
  CHECK_EQ((int) g.check(0x001E), (int) Deny::NOT_CONFIGURED);
  uint16_t bad[] = {0xC000, 0x0001}; g.set_configured(bad, 2); CHECK_EQ(g.configured_size(), 0u);  // non-unicast never enters the table
  CHECK_EQ((int) g.check(0x001E), (int) Deny::NOT_CONFIGURED);
}
TEST(release_follows_configured_table) {
  TransmitGuard g(false, true); uint16_t a[] = {0x001E}; g.set_configured(a, 1);
  CHECK_EQ((int) g.check(0x001E), (int) Deny::ALLOWED);
  uint16_t b[] = {0x000C}; g.set_configured(b, 1);  // the next boot's table replaces, never accumulates
  CHECK_EQ((int) g.check(0x001E), (int) Deny::NOT_CONFIGURED); CHECK_EQ((int) g.check(0x000C), (int) Deny::ALLOWED);
}
TEST(release_still_refuses_group) {
  TransmitGuard g(false, true); uint16_t t[] = {0x001E}; g.set_configured(t, 1);
  CHECK_EQ((int) g.check(0xC000), (int) Deny::GROUP_COMPILED_OUT); CHECK_EQ((int) g.check(0xFFFF), (int) Deny::GROUP_COMPILED_OUT);
  uint16_t al[] = {0x0010}; g.set_allowlist(al, 1);  // a dev allowlist means nothing in release mode
  CHECK_EQ((int) g.check(0x0010), (int) Deny::NOT_CONFIGURED);
}
TEST(production_allows_group_and_configured) {
  TransmitGuard g(true, false); uint16_t t[] = {0x001E}; g.set_configured(t, 1);  // allow_group implies release
  CHECK(!g.is_dev());
  CHECK_EQ((int) g.check(0xC000), (int) Deny::ALLOWED); CHECK_EQ((int) g.check(0xFFFF), (int) Deny::ALLOWED);
  CHECK_EQ((int) g.check(0x001E), (int) Deny::ALLOWED); CHECK_EQ((int) g.check(0x0010), (int) Deny::NOT_CONFIGURED);
}
TEST(mode_text) {
  CHECK(std::string(TransmitGuard(false, false).mode()) == "dev");
  CHECK(std::string(TransmitGuard(false, true).mode()) == "release");
  CHECK(std::string(TransmitGuard(true, true).mode()) == "production");
  CHECK(std::string(TransmitGuard::text(Deny::NOT_CONFIGURED)).size() > 0);
}
