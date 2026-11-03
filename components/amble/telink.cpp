#include "telink.h"

#include <cstring>

namespace esphome {
namespace amble {
namespace telink {

static uint8_t checksum(const uint8_t p[PAYLOAD_LEN]) {
  uint16_t s = 0;
  for (int i = 1; i < PAYLOAD_LEN; i++)
    s += p[i];
  return (uint8_t) (s & 0xff);
}

void status_request(uint8_t out[PAYLOAD_LEN]) {
  memset(out, 0, PAYLOAD_LEN);
  out[9] = 0x0e;
  out[0] = checksum(out);
}

void onoff(bool on, uint8_t out[PAYLOAD_LEN]) {
  memset(out, 0, PAYLOAD_LEN);
  out[8] = on ? 0x01 : 0x00;
  out[9] = 0x8c;
  out[0] = checksum(out);
}

void brightness(uint16_t intensity_0_1000, uint8_t out[PAYLOAD_LEN]) {
  uint16_t v = intensity_0_1000 > 1000 ? 1000 : intensity_0_1000;
  memset(out, 0, PAYLOAD_LEN);
  out[7] = (uint8_t) ((v & 3) << 6);
  out[8] = (uint8_t) ((v >> 2) & 0xff);
  out[9] = 0x8f;
  out[0] = checksum(out);
}

// The CCT field is kelvin/10; the +0x18 offset and wrap bit only apply above
// 10000 K; the green/magenta offset -10..+10 maps onto the fixture's 0..20 scale
// (neutral 10).
void cct(uint16_t kelvin, uint16_t intensity_0_1000, int gm_offset, uint8_t out[PAYLOAD_LEN]) {
  int v = intensity_0_1000 > 1000 ? 1000 : intensity_0_1000;
  int tcct = ((int) kelvin + 5) / 10;
  if (tcct < 80)
    tcct = 80;
  if (tcct > 2000)
    tcct = 2000;
  int g = gm_offset + 10;
  if (g < 0)
    g = 0;
  if (g > 20)
    g = 20;
  uint64_t low64 = ((uint64_t) (v & 0x03)) << 62;
  uint16_t high16 = (uint16_t) (0x8200 | ((v >> 2) & 0xff));
  if (tcct < 1001) {
    low64 |= (uint64_t) tcct << 52;
    high16 |= (uint16_t) ((tcct >> 12) & 0xff);
  } else {
    low64 |= ((uint64_t) ((tcct + 0x18) & 0x3ff)) << 52;
    low64 |= 0x0000040000000000ULL;
  }
  low64 |= (uint64_t) (g & 0x7f) << 45;
  for (int i = 0; i < 8; i++)
    out[i] = (uint8_t) ((low64 >> (i * 8)) & 0xff);
  out[8] = (uint8_t) (high16 & 0xff);
  out[9] = (uint8_t) ((high16 >> 8) & 0xff);
  out[0] = checksum(out);
}

void hsi(uint16_t hue_deg, uint8_t saturation_0_100, uint16_t intensity_0_1000, uint8_t out[PAYLOAD_LEN]) {
  uint16_t v = intensity_0_1000 > 1000 ? 1000 : intensity_0_1000;
  uint16_t h = (hue_deg > 360 ? 360 : hue_deg) & 0x1ff;
  uint8_t s = (saturation_0_100 > 100 ? 100 : saturation_0_100) & 0x7f;
  memset(out, 0, PAYLOAD_LEN);
  out[5] = (uint8_t) ((s & 3) << 6);
  out[6] = (uint8_t) (((h & 7) << 5) | ((s >> 2) & 0x1f));
  out[7] = (uint8_t) (((h >> 3) & 0x3f) | ((v & 3) << 6));
  out[8] = (uint8_t) ((v >> 2) & 0xff);
  out[9] = 0x81;
  out[0] = checksum(out);
}

bool decode_status(const uint8_t p[PAYLOAD_LEN], Status *out) {
  *out = Status{};
  if (p[0] != checksum(p))
    return false;
  uint8_t cmd = p[9] & 0x7f;
  out->command_type = cmd;
  uint64_t low64 = 0;
  for (int i = 0; i < 8; i++)
    low64 |= (uint64_t) p[i] << (i * 8);
  uint16_t high16 = (uint16_t) p[8] | ((uint16_t) p[9] << 8);
  out->on = (low64 >> 8) & 0x01;
  if (cmd == 0x02) {  // CCT mode
    out->is_hs = false;
    int cct_raw = (int) ((low64 >> 52) & 0x3ff);
    int cct_flag = (int) ((low64 >> 42) & 0x01);
    out->cct_kelvin = (uint16_t) ((cct_flag ? cct_raw + 1000 : cct_raw) * 10);
    out->intensity = (uint16_t) ((((uint32_t) high16 << 2) | (uint32_t) ((low64 >> 62) & 0x03)) & 0x3ff);
    int gm = (int) ((low64 >> 45) & 0x7f) - 10;
    out->gm = gm < -10 ? -10 : (gm > 10 ? 10 : gm);
    out->valid = true;
    return true;
  }
  if (cmd == 0x01) {  // HSI mode, same packing as the setter
    out->is_hs = true;
    uint16_t sat = (uint16_t) (((p[6] & 0x1f) << 2) | ((p[5] >> 6) & 0x03));
    uint16_t hue = (uint16_t) (((p[7] & 0x3f) << 3) | ((p[6] >> 5) & 0x07));
    uint16_t v = (uint16_t) (((uint16_t) p[8] << 2) | ((p[7] >> 6) & 0x03));
    out->sat = (uint8_t) (sat > 100 ? 100 : sat);
    out->hue = hue > 360 ? 360 : hue;
    out->intensity = v > 1000 ? 1000 : v;
    out->valid = true;
    return true;
  }
  return false;  // 0x0a diagnostic page and anything else: not decoded
}

const uint8_t *payload_in_access_pdu(const uint8_t *data, uint16_t len) {
  if (len == PAYLOAD_LEN + 1 && data[0] == OPCODE)
    return data + 1;
  if (len == PAYLOAD_LEN + 3 && data[0] == (0xC0 | OPCODE) && data[1] == (COMPANY_ID & 0xff) &&
      data[2] == (COMPANY_ID >> 8))
    return data + 3;
  return nullptr;
}

}  // namespace telink
}  // namespace amble
}  // namespace esphome
