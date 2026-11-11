#undef MINI_TEST_MAIN
#include <cmath>
#include "light.h"
#include "mini_test.h"

using namespace esphome::amble;

TEST(wire_intensity_rounds_and_clamps) {
  CHECK_EQ(wire_intensity_from_brightness(0.0f), 0); CHECK_EQ(wire_intensity_from_brightness(1.0f), 1000);
  CHECK_EQ(wire_intensity_from_brightness(0.5f), 500); CHECK_EQ(wire_intensity_from_brightness(1.5f), 1000);
  CHECK_EQ(wire_intensity_from_brightness(-0.2f), 0); CHECK_EQ(wire_intensity_from_brightness(0.6f), 600);
}
TEST(brightness_from_wire_inverse) { CHECK(fabsf(brightness_from_wire(600) - 0.6f) < 1e-3f); CHECK(fabsf(brightness_from_wire(2000) - 1.0f) < 1e-6f); CHECK(brightness_from_wire(0) == 0.0f); }
// ---- whole-percent fixtures: some keep only whole percent and truncate the rest, so
// 14.9 % comes back as 14.0 % and the slider springs back. The box learns it from the
// reply and sends the whole percent Home Assistant shows from then on. ---------------
TEST(truncated_reply_is_the_sent_value_cut_to_a_whole_percent) {
  CHECK(truncated_reply(149, 140)); CHECK(truncated_reply(39, 30)); CHECK(truncated_reply(4, 0));
  CHECK(!truncated_reply(149, 149));  // a fractional fixture answers with what it was sent
  CHECK(!truncated_reply(149, 150));  // rounding up is not truncation
  CHECK(!truncated_reply(150, 150));  // a whole percent proves nothing
  CHECK(!truncated_reply(150, 140));  // nor does some other state (the amaran app moved it)
  CHECK(!truncated_reply(149, 700));
}
TEST(whole_percent_rounds_to_what_the_slider_shows) {
  CHECK_EQ(whole_percent_intensity(149), 150); CHECK_EQ(whole_percent_intensity(145), 150);
  CHECK_EQ(whole_percent_intensity(144), 140); CHECK_EQ(whole_percent_intensity(150), 150);
  CHECK_EQ(whole_percent_intensity(1000), 1000); CHECK_EQ(whole_percent_intensity(996), 1000);
  CHECK_EQ(whole_percent_intensity(0), 0);
  CHECK_EQ(whole_percent_intensity(4), 10);  // the lowest step stays lit: 1 %, not 0
  CHECK_EQ(whole_percent_intensity(1), 10);
}
TEST(every_slider_percent_survives_a_whole_percent_fixture) {
  // Home Assistant: percent -> brightness 0..255 -> the box -> wire -> the fixture's reply
  // -> brightness 0..255 -> the percent shown. Every tick must come back as itself.
  for (int pct = 1; pct <= 100; pct++) {
    int ha = (int) lroundf(pct * 255.0f / 100.0f);
    uint16_t wire = whole_percent_intensity(wire_intensity_from_brightness(ha / 255.0f));
    CHECK_EQ(wire, pct * 10);
    int back = (int) lroundf(brightness_from_wire(wire) * 255.0f);
    CHECK_EQ((int) lroundf(back * 100.0f / 255.0f), pct);
  }
}
TEST(kelvin_mireds_roundtrip) {
  CHECK_EQ(kelvin_from_mireds(400.0f), 2500); CHECK(fabsf(mireds_from_kelvin(2500) - 400.0f) < 0.01f);
  CHECK(fabsf(mireds_from_kelvin(7500) - 133.33f) < 0.01f); CHECK_EQ(kelvin_from_mireds(mireds_from_kelvin(5600)), 5600);
  CHECK_EQ(kelvin_from_mireds(0.0f), 6500);
}
TEST(hsi_from_rgb_primaries) {
  uint16_t h; uint8_t s;
  hsi_from_rgb(1, 0, 0, &h, &s); CHECK_EQ(h, 0); CHECK_EQ(s, 100);
  hsi_from_rgb(0, 1, 0, &h, &s); CHECK_EQ(h, 120); CHECK_EQ(s, 100);
  hsi_from_rgb(0, 0, 1, &h, &s); CHECK_EQ(h, 240); CHECK_EQ(s, 100);
  hsi_from_rgb(1, 1, 1, &h, &s); CHECK_EQ(s, 0);
  hsi_from_rgb(1, 0.5f, 0.5f, &h, &s); CHECK_EQ(h, 0); CHECK_EQ(s, 50);
}
TEST(rgb_from_hs_roundtrip) {
  for (uint16_t hue : {0, 60, 120, 180, 240, 300, 359}) {
    float r, g, b; rgb_from_hs(hue, 100, &r, &g, &b); uint16_t h2; uint8_t s2; hsi_from_rgb(r, g, b, &h2, &s2);
    CHECK_EQ(h2, hue); CHECK_EQ(s2, 100);
  }
  float r, g, b; rgb_from_hs(200, 0, &r, &g, &b); CHECK(r == 1.0f && g == 1.0f && b == 1.0f);
}

// ---- plan_sends: what a command actually puts on the air (bench finding 2026-10-26:
// the fixture keeps its look while off and reports it) ------------------------------
static Look cct(uint16_t i, uint16_t k, int gm = 0) { Look l; l.hsi = false; l.intensity = i; l.kelvin = k; l.gm = gm; return l; }
static Look hsi(uint16_t i, uint16_t h, uint8_t s) { Look l; l.hsi = true; l.intensity = i; l.hue = h; l.sat = s; return l; }

TEST(plan_power_on_without_reference_sends_power_only) {
  SendPlan p = plan_sends(false, Look{}, false, true, cct(1000, 7500));
  CHECK(p.power_on); CHECK(!p.look); CHECK(!p.power_off);
}
TEST(plan_turn_on_same_look_sends_power_only) {
  SendPlan p = plan_sends(true, cct(200, 3200), false, true, cct(200, 3200));
  CHECK(p.power_on); CHECK(!p.look); CHECK(!p.power_off);
}
TEST(plan_turn_on_new_look_sends_power_and_look) {
  SendPlan p = plan_sends(true, cct(200, 3200), false, true, cct(600, 3200));
  CHECK(p.power_on); CHECK(p.look); CHECK(!p.power_off);
}
TEST(plan_changed_look_while_on_sends_look_only) {
  SendPlan p = plan_sends(true, cct(200, 3200), true, true, hsi(200, 0, 100));
  CHECK(!p.power_on); CHECK(p.look); CHECK(!p.power_off);
  SendPlan g = plan_sends(true, cct(200, 3200, 0), true, true, cct(200, 3200, 3));
  CHECK(g.look);  // green/magenta is part of the look
}
TEST(plan_identical_state_sends_nothing) {
  SendPlan p = plan_sends(true, hsi(200, 0, 100), true, true, hsi(200, 0, 100));
  CHECK(!p.power_on); CHECK(!p.look); CHECK(!p.power_off);
}
TEST(plan_off_restores_look_then_power_off) {
  SendPlan p = plan_sends(true, cct(500, 5600), true, false, cct(200, 3200));
  CHECK(!p.power_on); CHECK(p.look); CHECK(p.power_off);
}
TEST(plan_off_same_look_sends_power_off_only) {
  SendPlan p = plan_sends(true, cct(200, 3200), true, false, cct(200, 3200));
  CHECK(!p.look); CHECK(p.power_off);
  SendPlan q = plan_sends(false, Look{}, false, false, cct(200, 3200));  // never heard: still say off
  CHECK(!q.look); CHECK(q.power_off);
}
TEST(look_from_status_maps_both_modes) {
  esphome::amble::telink::Status st; st.valid = true; st.on = true; st.is_hs = false; st.intensity = 200; st.cct_kelvin = 3200; st.gm = -2;
  Look l = look_from_status(st); CHECK(!l.hsi); CHECK_EQ(l.intensity, 200); CHECK_EQ(l.kelvin, 3200); CHECK_EQ(l.gm, -2);
  st.is_hs = true; st.hue = 120; st.sat = 50;
  Look m = look_from_status(st); CHECK(m.hsi); CHECK_EQ(m.hue, 120); CHECK_EQ(m.sat, 50);
  CHECK(same_look(l, l)); CHECK(!same_look(l, m)); CHECK(!same_look(cct(200, 3200), cct(200, 3201)));
}
