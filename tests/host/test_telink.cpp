#undef MINI_TEST_MAIN
#include <cstring>
#include "mini_test.h"
#include "telink.h"

using namespace esphome::amble;

static void as_report(uint8_t p[10], uint8_t type) {
  p[9] = type;
  p[0] = 0;
  for (int i = 1; i < 10; i++) p[0] += p[i];
}

TEST(status_request_is_0e_with_checksum) { uint8_t p[10]; telink::status_request(p); CHECK_EQ(p[9], 0x0e); CHECK_EQ(p[0], 0x0e); }
TEST(onoff_shape) { uint8_t p[10]; telink::onoff(true, p); CHECK_EQ(p[8], 1); CHECK_EQ(p[9], 0x8c); CHECK_EQ(p[0], (uint8_t) (1 + 0x8c)); telink::onoff(false, p); CHECK_EQ(p[8], 0); }
TEST(brightness_1000_packs_high_bits) { uint8_t p[10]; telink::brightness(1000, p); CHECK_EQ(p[7], (uint8_t) ((1000 & 3) << 6)); CHECK_EQ(p[8], (uint8_t) (1000 >> 2)); CHECK_EQ(p[9], 0x8f); }
TEST(brightness_clamps_above_1000) { uint8_t a[10], b[10]; telink::brightness(1000, a); telink::brightness(5000, b); CHECK(memcmp(a, b, 10) == 0); }
TEST(cct_5600k_decodes_back) { uint8_t p[10]; telink::cct(5600, 500, 0, p); as_report(p, 0x02); telink::Status s; CHECK(telink::decode_status(p, &s)); CHECK_EQ(s.cct_kelvin, 5600); CHECK_EQ(s.intensity, 500); CHECK_EQ(s.gm, 0); CHECK(!s.is_hs); }
TEST(cct_3200k_decodes_back) { uint8_t p[10]; telink::cct(3200, 600, 0, p); as_report(p, 0x02); telink::Status s; CHECK(telink::decode_status(p, &s)); CHECK_EQ(s.cct_kelvin, 3200); CHECK_EQ(s.intensity, 600); }
TEST(cct_gm_offset_maps_to_0_20_scale) {
  uint8_t p[10]; uint64_t low;
  telink::cct(5600, 500, -10, p); low = 0; for (int i = 0; i < 8; i++) low |= (uint64_t) p[i] << (i * 8); CHECK_EQ((int) ((low >> 45) & 0x7f), 0);
  telink::cct(5600, 500, 10, p); low = 0; for (int i = 0; i < 8; i++) low |= (uint64_t) p[i] << (i * 8); CHECK_EQ((int) ((low >> 45) & 0x7f), 20);
  telink::cct(5600, 500, 99, p); low = 0; for (int i = 0; i < 8; i++) low |= (uint64_t) p[i] << (i * 8); CHECK_EQ((int) ((low >> 45) & 0x7f), 20);
}
TEST(cct_gm_decodes_back) { uint8_t p[10]; telink::cct(5600, 500, -3, p); as_report(p, 0x02); telink::Status s; CHECK(telink::decode_status(p, &s)); CHECK_EQ(s.gm, -3); }
TEST(hsi_decodes_back) { uint8_t p[10]; telink::hsi(120, 100, 600, p); as_report(p, 0x01); telink::Status s; CHECK(telink::decode_status(p, &s)); CHECK(s.is_hs); CHECK_EQ(s.hue, 120); CHECK_EQ(s.sat, 100); CHECK_EQ(s.intensity, 600); }
TEST(hsi_clamps) { uint8_t p[10]; telink::hsi(400, 150, 2000, p); as_report(p, 0x01); telink::Status s; CHECK(telink::decode_status(p, &s)); CHECK_EQ(s.hue, 360); CHECK_EQ(s.sat, 100); CHECK_EQ(s.intensity, 1000); }
TEST(decode_rejects_bad_checksum) { uint8_t p[10] = {0}; p[9] = 0x02; p[0] = 0x55; telink::Status s; CHECK(!telink::decode_status(p, &s)); CHECK(!s.valid); }
TEST(decode_ignores_diagnostic_page_0a) { uint8_t p[10] = {0}; p[9] = 0x0a; p[0] = 0x0a; telink::Status s; CHECK(!telink::decode_status(p, &s)); CHECK_EQ(s.command_type, 0x0a); }
TEST(decode_reports_power_bit) { uint8_t p[10]; telink::cct(5600, 500, 0, p); p[1] |= 0x01; as_report(p, 0x02); telink::Status s; CHECK(telink::decode_status(p, &s)); CHECK(s.on); }
TEST(payload_in_access_pdu_1_byte_opcode) { uint8_t d[11] = {0x26}; CHECK(telink::payload_in_access_pdu(d, 11) == d + 1); }
TEST(payload_in_access_pdu_3_byte_opcode) { uint8_t d[13] = {0xE6, 0x11, 0x02}; CHECK(telink::payload_in_access_pdu(d, 13) == d + 3); }
TEST(payload_in_access_pdu_rejects_other) { uint8_t d[11] = {0x82}; CHECK(telink::payload_in_access_pdu(d, 11) == nullptr); CHECK(telink::payload_in_access_pdu(d, 5) == nullptr); uint8_t e[13] = {0xE6, 0x12, 0x02}; CHECK(telink::payload_in_access_pdu(e, 13) == nullptr); }
