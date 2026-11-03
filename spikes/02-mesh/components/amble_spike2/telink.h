#pragma once
// Telink vendor-opcode payload builders and status decoder, ported from the
// studio bridge's telink.c (docs/origin.md item 3). All payloads are 10 bytes:
// p[0] = checksum of p[1..9], p[9] = command type.
#include <cstdint>
#include "amble_spike2.h"

namespace esphome {
namespace amble_spike2 {
namespace telink {

constexpr uint16_t COMPANY_ID = 0x0211;
constexpr uint16_t VENDOR_MODEL_ID = 0x0000;
constexpr uint8_t OPCODE = 0x26;

void status_request(uint8_t out[10]);
void onoff(bool on, uint8_t out[10]);
void brightness(uint16_t intensity_0_1000, uint8_t out[10]);
void cct(uint16_t kelvin, uint16_t intensity_0_1000, int gm_offset, uint8_t out[10]);
void hsi(uint16_t hue_deg, uint8_t saturation_0_100, uint16_t intensity_0_1000, uint8_t out[10]);
bool decode_status(const uint8_t p[10], FixtureStatus *out);

}  // namespace telink
}  // namespace amble_spike2
}  // namespace esphome
