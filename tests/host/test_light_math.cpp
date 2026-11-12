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

// ---- look_lost: did the first reply after a look command show the command arrived? ----
TEST(look_lost_reply_matching_the_command_is_not_lost) {
  CHECK(!look_lost(cct(500, 5600), cct(500, 5600), true, cct(200, 3200)));
  CHECK(!look_lost(cct(500, 5600), cct(500, 5600), false, Look{}));
  // Sent back to the look last reported (a quick there-and-back): arrived.
  CHECK(!look_lost(cct(200, 3200), cct(200, 3200), true, cct(200, 3200)));
}
TEST(look_lost_old_look_in_the_reply_is_lost) {
  CHECK(look_lost(cct(500, 5600), cct(200, 3200), true, cct(200, 3200)));
  CHECK(look_lost(cct(200, 5600), cct(200, 3200), true, cct(200, 3200)));     // colour-only change, lost
  CHECK(look_lost(hsi(200, 120, 100), hsi(200, 0, 100), true, hsi(200, 0, 100)));
}
TEST(look_lost_other_brightness_or_mode_is_lost_even_with_nothing_to_compare) {
  CHECK(look_lost(cct(500, 5600), cct(300, 5600), false, Look{}));
  CHECK(look_lost(cct(500, 5600), cct(300, 5600), true, cct(200, 3200)));     // an earlier command stuck, this one did not
  CHECK(look_lost(hsi(500, 120, 100), cct(500, 5600), false, Look{}));
}
TEST(look_lost_a_fixture_rounding_the_command_is_not_a_loss) {
  CHECK(!look_lost(cct(149, 5600), cct(140, 5600), true, cct(200, 3200)));    // whole-percent cut
  CHECK(!look_lost(cct(500, 3984), cct(500, 4000), true, cct(200, 3200)));    // rounded kelvin
  CHECK(!look_lost(hsi(500, 121, 57), hsi(500, 120, 57), true, cct(200, 3200)));
  CHECK(!look_lost(cct(500, 3984), cct(500, 4000), false, Look{}));
}

// ---- review D1: a request the wire cannot tell from the current look is never sent ----
TEST(wire_look_is_what_the_encoder_sends) {
  CHECK(same_look(wire_look(cct(500, 4004)), cct(500, 4000)));
  CHECK(same_look(wire_look(cct(500, 4005)), cct(500, 4010)));
  CHECK(same_look(wire_look(cct(1200, 500, 14)), cct(1000, 800, 10)));
  CHECK(same_look(wire_look(cct(500, 25000, -14)), cct(500, 20000, -10)));
  CHECK(same_look(wire_look(hsi(500, 400, 120)), hsi(500, 360, 100)));
  CHECK(same_look(wire_look(hsi(500, 120, 57)), hsi(500, 120, 57)));
}
TEST(four_kelvin_from_the_current_look_sends_nothing_and_confirms_nothing) {
  Look current = cct(500, 4000), target = wire_look(cct(500, 4004));
  SendPlan p = plan_sends(true, current, true, true, target);
  CHECK(!p.power_on); CHECK(!p.look); CHECK(!p.power_off);
  CHECK(!look_lost(target, cct(500, 4000), true, current));   // and were it sent, the reply matches it
}
// ---- review D2: two quick commands, the first arrived and the second was lost. The reply
// shows the first command's look, which is the reference the second one replaced. ----
TEST(look_lost_when_the_reply_shows_the_command_before) {
  Look reported = cct(500, 3200), first = cct(500, 4000), second = cct(500, 5600), reply = cct(500, 4000);
  CHECK(!look_lost(second, reply, true, reported));           // the last report alone cannot tell
  CHECK(look_lost(second, reply, true, first));               // the look just before does
  Look r2 = hsi(500, 0, 100), f2 = hsi(500, 120, 100), s2 = hsi(500, 240, 100);
  CHECK(!look_lost(s2, f2, true, r2));
  CHECK(look_lost(s2, f2, true, f2));
  CHECK(!look_lost(second, second, true, first));             // arrived: not lost whatever came before
}
