#pragma once
// Fixture table and the import payload parser. Pure: no ESPHome or IDF
// includes, so it is unit-tested on the host. `Config` is a fixed-size POD —
// it is exactly what the NVS store persists (store.h).
#include <cstddef>
#include <cstdint>
#include <string>

namespace esphome {
namespace amble {

constexpr uint8_t MAX_FIXTURES = 32;
constexpr size_t NAME_LEN = 32;  // including the terminating NUL
// The box's own unicast addresses live in the top half of the unicast space,
// above anything the amaran app assigns (it numbers fixtures from the bottom).
constexpr uint16_t OWN_ADDRESS_BASE = 0x4000;
constexpr uint16_t OWN_ADDRESS_MASK = 0x3FFF;  // 16384 slots: 0x4000..0x7FFF

struct Caps {
  bool color{false};
  bool gm{false};
  uint16_t cct_min{2700};
  uint16_t cct_max{6500};
};

struct Fixture {
  uint8_t mac[6]{};
  uint16_t address{0};
  char name[NAME_LEN]{};
  Caps caps{};
};

struct Config {
  uint8_t net_key[16]{};
  uint8_t app_key[16]{};
  uint16_t own_address{0};  // filled at boot from the box's address seed (derive_own_address); 0 = none
  uint8_t fixture_count{0};
  Fixture fixtures[MAX_FIXTURES]{};
};

enum class ParseError : uint8_t {
  OK,
  BAD_JSON,
  BAD_VERSION,
  BAD_KEY,
  BAD_ADDRESS,
  DUPLICATE_ADDRESS,
  BAD_MAC,
  TOO_MANY,
  BAD_CCT,
  EMPTY,
  DUPLICATE_ID,
};

// Parses the version-1 import payload (web/importer toImportPayload). On any
// error *out is left untouched and a fixed-text error is returned; the input is
// never echoed. The payload never carries the box's own address: a payload that
// tries (`own_address`) is refused, because identity is derived, not imported.
ParseError parse_import_payload(const char *json, size_t len, Config *out);

// The box's own mesh address: a 16-bit seed folded into 0x4000..0x7FFF, then
// stepped past any fixture that already holds it. The seed is random,
// chosen once on the box's first boot and kept in NVS (amble.cpp), so the
// address is stable for the life of the configuration and an erase gives the
// box a genuinely new identity (review D4: a box returning at a known address
// with a restarted sequence number would replay). Two boxes share an address
// only by a 1-in-16384 chance, unguarded. No amaran-assigned fixture address is
// ever taken.
uint16_t derive_own_address(uint16_t seed, const Fixture *fixtures, uint8_t count);
const char *parse_error_text(ParseError e);

// Unicast check shared by the parser and the transmit guard: 0x0002..0x7FFF.
inline bool is_unicast(uint16_t a) { return a >= 0x0002 && a < 0x8000; }

// Stable, name-independent entity object id: "amble_" + last three MAC bytes, lower-case hex.
std::string object_id_for(const Fixture &f);

}  // namespace amble
}  // namespace esphome
