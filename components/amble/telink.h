#pragma once
// Telink vendor-opcode (0x26) payload codec for amaran fixtures. Ported
// byte-for-byte from the studio bridge's telink.c (docs/origin.md item 3) and
// proven against the P60c in Spike 2. Pure: no ESPHome or IDF includes.
//
// Every payload is 10 bytes: p[0] = checksum of p[1..9] (sum & 0xff),
// p[9] = command type (bit 7 set on commands, clear on fixture reports).
#include <cstdint>

namespace esphome {
namespace amble {
namespace telink {

constexpr uint16_t COMPANY_ID = 0x0211;
constexpr uint16_t VENDOR_MODEL_ID = 0x0000;
constexpr uint8_t OPCODE = 0x26;
constexpr uint8_t PAYLOAD_LEN = 10;

struct Status {
  bool valid{false};
  bool on{false};
  bool is_hs{false};
  uint16_t intensity{0};   // 0..1000
  uint16_t cct_kelvin{0};  // CCT mode
  int gm{0};               // -10..+10, 0 neutral (CCT mode)
  uint16_t hue{0};         // 0..360 (HSI mode)
  uint8_t sat{0};          // 0..100 (HSI mode)
  uint8_t command_type{0}; // raw p[9] & 0x7f
};

void status_request(uint8_t out[PAYLOAD_LEN]);
void onoff(bool on, uint8_t out[PAYLOAD_LEN]);
void brightness(uint16_t intensity_0_1000, uint8_t out[PAYLOAD_LEN]);
void cct(uint16_t kelvin, uint16_t intensity_0_1000, int gm_offset, uint8_t out[PAYLOAD_LEN]);
void hsi(uint16_t hue_deg, uint8_t saturation_0_100, uint16_t intensity_0_1000, uint8_t out[PAYLOAD_LEN]);

// Decodes a fixture state report. Returns true and fills *out only for state
// reports (command types 0x01 HSI / 0x02 CCT) with a valid checksum.
bool decode_status(const uint8_t p[PAYLOAD_LEN], Status *out);

// Locates the 10-byte Telink payload inside a decrypted access PDU: either the
// 1-byte opcode form (0x26 + 10 bytes) the fixtures use, or the 3-byte vendor
// form (0xC0|0x26, company id LE + 10 bytes). nullptr if neither.
const uint8_t *payload_in_access_pdu(const uint8_t *data, uint16_t len);

}  // namespace telink
}  // namespace amble
}  // namespace esphome
