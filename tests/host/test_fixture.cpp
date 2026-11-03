#undef MINI_TEST_MAIN
#include <cstring>
#include <string>
#include "fixture.h"
#include "mini_test.h"

using namespace esphome::amble;

static const char *GOOD =
    R"({"version":1,"net_key":"00112233445566778899aabbccddeeff","app_key":"ffeeddccbbaa99887766554433221100",)"
    R"("fixtures":[{"mac":"AA:BB:CC:DD:EE:FF","address":30,"name":"amaran P60c","color":true,"gm":true,"cct_min":2500,"cct_max":7500}]})";

static std::string with(const char *fixtures_json, const char *extra = "") {
  std::string s = R"({"version":1,"net_key":"00112233445566778899aabbccddeeff","app_key":"ffeeddccbbaa99887766554433221100",)";
  s += extra;
  s += R"("fixtures":)";
  s += fixtures_json;
  s += "}";
  return s;
}
static ParseError parse(const std::string &s, Config *c) { return parse_import_payload(s.c_str(), s.size(), c); }

TEST(parse_good_payload) {
  Config c{};
  CHECK_EQ((int) parse(GOOD, &c), (int) ParseError::OK);
  CHECK_EQ(c.fixture_count, 1);
  CHECK_EQ(c.fixtures[0].address, 30);
  CHECK_EQ(c.net_key[0], 0x00); CHECK_EQ(c.net_key[15], 0xff); CHECK_EQ(c.app_key[0], 0xff); CHECK_EQ(c.app_key[15], 0x00);
  CHECK_EQ(c.own_address, 0);  // derived at boot, never from the payload
  CHECK(c.fixtures[0].caps.color); CHECK(c.fixtures[0].caps.gm);
  CHECK_EQ(c.fixtures[0].caps.cct_min, 2500); CHECK_EQ(c.fixtures[0].caps.cct_max, 7500);
  CHECK_EQ(c.fixtures[0].mac[0], 0xAA); CHECK_EQ(c.fixtures[0].mac[5], 0xFF);
  CHECK(strcmp(c.fixtures[0].name, "amaran P60c") == 0);
}
TEST(parse_leaves_output_untouched_on_error) {
  Config c{}; c.fixture_count = 7;
  CHECK_EQ((int) parse(with(R"([{"mac":"AA:BB:CC:DD:EE:FF","address":49152}])"), &c), (int) ParseError::BAD_ADDRESS);
  CHECK_EQ(c.fixture_count, 7);
}
TEST(parse_rejects_bad_address) {
  Config c{};
  CHECK_EQ((int) parse(with(R"([{"mac":"AA:BB:CC:DD:EE:FF","address":49152}])"), &c), (int) ParseError::BAD_ADDRESS);  // 0xC000
  CHECK_EQ((int) parse(with(R"([{"mac":"AA:BB:CC:DD:EE:FF","address":1}])"), &c), (int) ParseError::BAD_ADDRESS);
  CHECK_EQ((int) parse(with(R"([{"mac":"AA:BB:CC:DD:EE:FF","address":65535}])"), &c), (int) ParseError::BAD_ADDRESS);
}
TEST(parse_rejects_duplicate_address) {
  Config c{};
  CHECK_EQ((int) parse(with(R"([{"mac":"AA:BB:CC:DD:EE:01","address":30},{"mac":"AA:BB:CC:DD:EE:02","address":30}])"), &c), (int) ParseError::DUPLICATE_ADDRESS);
}
TEST(parse_rejects_duplicate_mac) {
  Config c{};
  CHECK_EQ((int) parse(with(R"([{"mac":"AA:BB:CC:DD:EE:01","address":30},{"mac":"aa:bb:cc:dd:ee:01","address":31}])"), &c), (int) ParseError::BAD_MAC);
}
TEST(parse_rejects_short_key) {
  Config c{};
  std::string s = R"({"version":1,"net_key":"00112233445566778899aabbccddee","app_key":"ffeeddccbbaa99887766554433221100","fixtures":[{"mac":"AA:BB:CC:DD:EE:FF","address":30}]})";
  CHECK_EQ((int) parse(s, &c), (int) ParseError::BAD_KEY);
  std::string t = R"({"version":1,"net_key":"0011223344556677gg99aabbccddeeff","app_key":"ffeeddccbbaa99887766554433221100","fixtures":[{"mac":"AA:BB:CC:DD:EE:FF","address":30}]})";
  CHECK_EQ((int) parse(t, &c), (int) ParseError::BAD_KEY);
}
TEST(parse_rejects_wrong_version) {
  Config c{};
  std::string s = R"({"version":2,"net_key":"00112233445566778899aabbccddeeff","app_key":"ffeeddccbbaa99887766554433221100","fixtures":[{"mac":"AA:BB:CC:DD:EE:FF","address":30}]})";
  CHECK_EQ((int) parse(s, &c), (int) ParseError::BAD_VERSION);
}
TEST(parse_rejects_garbage) { Config c{}; CHECK_EQ((int) parse("not json", &c), (int) ParseError::BAD_JSON); CHECK_EQ((int) parse("[1,2]", &c), (int) ParseError::BAD_JSON); }
TEST(parse_rejects_more_than_32) {
  std::string arr = "[";
  for (int i = 0; i < 33; i++) { char b[80]; snprintf(b, sizeof(b), R"(%s{"mac":"AA:BB:CC:DD:%02X:%02X","address":%d})", i ? "," : "", i, i, 100 + i); arr += b; }
  arr += "]";
  Config c{}; CHECK_EQ((int) parse(with(arr.c_str()), &c), (int) ParseError::TOO_MANY);
}
TEST(parse_accepts_exactly_32) {
  std::string arr = "[";
  for (int i = 0; i < 32; i++) { char b[80]; snprintf(b, sizeof(b), R"(%s{"mac":"AA:BB:CC:DD:%02X:%02X","address":%d})", i ? "," : "", i, i, 100 + i); arr += b; }
  arr += "]";
  Config c{}; CHECK_EQ((int) parse(with(arr.c_str()), &c), (int) ParseError::OK); CHECK_EQ(c.fixture_count, 32);
}
TEST(parse_rejects_empty_fixture_list) { Config c{}; CHECK_EQ((int) parse(with("[]"), &c), (int) ParseError::EMPTY); }
TEST(parse_rejects_inverted_cct) { Config c{}; CHECK_EQ((int) parse(with(R"([{"mac":"AA:BB:CC:DD:EE:FF","address":30,"cct_min":7500,"cct_max":2500}])"), &c), (int) ParseError::BAD_CCT); }
TEST(parse_defaults_caps_when_absent) {
  Config c{}; CHECK_EQ((int) parse(with(R"([{"mac":"AA:BB:CC:DD:EE:FF","address":30}])"), &c), (int) ParseError::OK);
  CHECK(!c.fixtures[0].caps.color); CHECK(!c.fixtures[0].caps.gm); CHECK_EQ(c.fixtures[0].caps.cct_min, 2700); CHECK_EQ(c.fixtures[0].caps.cct_max, 6500);
  CHECK(strcmp(c.fixtures[0].name, "Light 001E") == 0);
}
TEST(parse_truncates_long_name_safely) {
  std::string name(60, 'x');
  Config c{}; CHECK_EQ((int) parse(with((R"([{"mac":"AA:BB:CC:DD:EE:FF","address":30,"name":")" + name + R"("}])").c_str()), &c), (int) ParseError::OK);
  CHECK_EQ(strlen(c.fixtures[0].name), NAME_LEN - 1); CHECK_EQ(c.fixtures[0].name[NAME_LEN - 1], 0);
}
TEST(parse_refuses_own_address_in_payload) {
  Config c{};
  CHECK_EQ((int) parse(with(R"([{"mac":"AA:BB:CC:DD:EE:FF","address":30}])", R"("own_address":242,)"), &c), (int) ParseError::BAD_ADDRESS);
  CHECK_EQ((int) parse(with(R"([{"mac":"AA:BB:CC:DD:EE:FF","address":30}])", R"("own_address":16400,)"), &c), (int) ParseError::BAD_ADDRESS);
}
TEST(derive_own_address_folds_the_seed_into_the_upper_range) {
  CHECK_EQ(derive_own_address(0xB2C3, nullptr, 0), 0x72C3);  // 0xB2C3 & 0x3FFF = 0x32C3, | 0x4000
  CHECK_EQ(derive_own_address(0xFFFF, nullptr, 0), 0x7FFF);
  CHECK_EQ(derive_own_address(0x0000, nullptr, 0), 0x4000);
  CHECK(derive_own_address(0xB2C4, nullptr, 0) != derive_own_address(0xB2C3, nullptr, 0));
}
TEST(derive_own_address_steps_past_a_fixture_holding_it) {
  Fixture f[3]{}; f[0].address = 0x72C3; f[1].address = 0x72C4; f[2].address = 0x0010;
  CHECK_EQ(derive_own_address(0xB2C3, f, 3), 0x72C5);
  Fixture g[1]{}; g[0].address = 0x7FFF;
  CHECK_EQ(derive_own_address(0xFFFF, g, 1), 0x4000);  // wraps inside the range, never below it
}
TEST(object_id_from_mac_ignores_name) {
  Fixture a{}; const uint8_t m[6] = {0xAA, 0xBB, 0xCC, 0x8D, 0xED, 0xB3}; memcpy(a.mac, m, 6); strcpy(a.name, "Key");
  Fixture b = a; b.mac[5] = 0xB4; strcpy(b.name, "Key");
  CHECK(object_id_for(a) == "amble_8dedb3"); CHECK(object_id_for(b) == "amble_8dedb4"); CHECK(object_id_for(a) != object_id_for(b));
}
TEST(error_text_is_fixed) { CHECK(strcmp(parse_error_text(ParseError::BAD_KEY), "net_key or app_key is not 16 bytes of hex") == 0); }
TEST(is_unicast_bounds) { CHECK(!is_unicast(0)); CHECK(!is_unicast(1)); CHECK(is_unicast(2)); CHECK(is_unicast(0x7FFF)); CHECK(!is_unicast(0x8000)); CHECK(!is_unicast(0xC000)); CHECK(!is_unicast(0xFFFF)); }

TEST(parse_rejects_fractional_numbers) {
  Config c{};
  std::string v = R"({"version":1.5,"net_key":"00112233445566778899aabbccddeeff","app_key":"ffeeddccbbaa99887766554433221100","fixtures":[{"mac":"AA:BB:CC:DD:EE:FF","address":30}]})";
  CHECK_EQ((int) parse(v, &c), (int) ParseError::BAD_VERSION);
  CHECK_EQ((int) parse(with(R"([{"mac":"AA:BB:CC:DD:EE:FF","address":30.9}])"), &c), (int) ParseError::BAD_ADDRESS);
  CHECK_EQ((int) parse(with(R"([{"mac":"AA:BB:CC:DD:EE:FF","address":30,"cct_min":2500.5,"cct_max":7500}])"), &c), (int) ParseError::BAD_CCT);
  CHECK_EQ(c.fixture_count, 0);
}
TEST(parse_rejects_mac_suffix_collision) {
  Config c{};
  CHECK_EQ((int) parse(with(R"([{"mac":"AA:BB:CC:DD:EE:01","address":30},{"mac":"11:22:33:DD:EE:01","address":31}])"), &c), (int) ParseError::DUPLICATE_ID);
}

// #33: a name over 31 bytes is cut on a character boundary, never inside a UTF-8 sequence.
static std::string payload_with_name(const std::string &name) {
  return with((R"([{"mac":"AA:BB:CC:DD:EE:FF","address":30,"name":")" + name + R"("}])").c_str());
}
TEST(parse_cuts_long_cjk_name_on_a_character_boundary) {
  std::string lamp = "\xE7\x81\xAF";  // 灯, three bytes
  std::string name; for (int i = 0; i < 11; i++) name += lamp;  // 33 bytes
  Config c{}; CHECK_EQ((int) parse(payload_with_name(name), &c), (int) ParseError::OK);
  std::string expect; for (int i = 0; i < 10; i++) expect += lamp;  // ten whole characters, 30 bytes
  CHECK(std::string(c.fixtures[0].name) == expect);
}
TEST(parse_cut_landing_inside_a_character_backs_up_to_its_start) {
  std::string name(29, 'a'); name += "\xE7\x81\xAF";  // 32 bytes: the 31-byte cut would land after the character's first byte
  Config c{}; CHECK_EQ((int) parse(payload_with_name(name), &c), (int) ParseError::OK);
  CHECK(std::string(c.fixtures[0].name) == std::string(29, 'a'));
}
TEST(parse_keeps_a_name_of_exactly_31_bytes_whole) {
  std::string name(28, 'b'); name += "\xE7\x81\xAF";  // 31 bytes
  Config c{}; CHECK_EQ((int) parse(payload_with_name(name), &c), (int) ParseError::OK);
  CHECK(std::string(c.fixtures[0].name) == name);
}
TEST(parse_long_name_of_only_continuation_bytes_falls_back_to_the_default) {
  Config c{}; CHECK_EQ((int) parse(payload_with_name(std::string(40, '\x80')), &c), (int) ParseError::OK);  // the cut backs up to nothing
  CHECK(strcmp(c.fixtures[0].name, "Light 001E") == 0);
}
