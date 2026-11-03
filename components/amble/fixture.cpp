#include "fixture.h"

#include <cstdio>
#include <cstring>
#include <memory>

#include <ArduinoJson.h>

namespace esphome {
namespace amble {

static int hex_nibble(char c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return -1;
}

static bool parse_hex_bytes(const char *s, size_t n_bytes, uint8_t *out) {
  if (s == nullptr || strlen(s) != n_bytes * 2)
    return false;
  for (size_t i = 0; i < n_bytes; i++) {
    int hi = hex_nibble(s[2 * i]), lo = hex_nibble(s[2 * i + 1]);
    if (hi < 0 || lo < 0)
      return false;
    out[i] = (uint8_t) ((hi << 4) | lo);
  }
  return true;
}

// "AA:BB:CC:DD:EE:FF" (also '-' separators, either case)
static bool parse_mac(const char *s, uint8_t out[6]) {
  if (s == nullptr || strlen(s) != 17)
    return false;
  for (int i = 0; i < 6; i++) {
    int hi = hex_nibble(s[3 * i]), lo = hex_nibble(s[3 * i + 1]);
    if (hi < 0 || lo < 0)
      return false;
    if (i < 5 && s[3 * i + 2] != ':' && s[3 * i + 2] != '-')
      return false;
    out[i] = (uint8_t) ((hi << 4) | lo);
  }
  return true;
}

ParseError parse_import_payload(const char *json, size_t len, Config *out) {
  JsonDocument doc;
  if (deserializeJson(doc, json, len) != DeserializationError::Ok || !doc.is<JsonObject>())
    return ParseError::BAD_JSON;
  JsonObject root = doc.as<JsonObject>();
  if (!root["version"].is<int>() || root["version"].as<int>() != 1)
    return ParseError::BAD_VERSION;

  auto cp = std::make_unique<Config>();  // ~1.6 KB: heap, not the caller's stack
  Config &c = *cp;
  if (!parse_hex_bytes(root["net_key"].as<const char *>(), 16, c.net_key) ||
      !parse_hex_bytes(root["app_key"].as<const char *>(), 16, c.app_key))
    return ParseError::BAD_KEY;

  if (!root["own_address"].isNull())
    return ParseError::BAD_ADDRESS;  // the box derives its own address; never imported

  JsonArray fixtures = root["fixtures"].as<JsonArray>();
  if (fixtures.isNull() || fixtures.size() == 0)
    return ParseError::EMPTY;
  if (fixtures.size() > MAX_FIXTURES)
    return ParseError::TOO_MANY;

  for (JsonObject f : fixtures) {
    Fixture &fx = c.fixtures[c.fixture_count];
    if (!parse_mac(f["mac"].as<const char *>(), fx.mac))
      return ParseError::BAD_MAC;
    if (!f["address"].is<long>())
      return ParseError::BAD_ADDRESS;
    long addr = f["address"].as<long>();
    if (addr < 0x0002 || addr >= 0x8000)
      return ParseError::BAD_ADDRESS;
    for (uint8_t i = 0; i < c.fixture_count; i++) {
      if (c.fixtures[i].address == addr)
        return ParseError::DUPLICATE_ADDRESS;
      if (memcmp(c.fixtures[i].mac, fx.mac, 6) == 0)
        return ParseError::BAD_MAC;
      if (memcmp(c.fixtures[i].mac + 3, fx.mac + 3, 3) == 0)
        return ParseError::DUPLICATE_ID;  // object ids use the last three bytes
    }
    fx.address = (uint16_t) addr;
    const char *name = f["name"].as<const char *>();
    if (name == nullptr || name[0] == '\0') {
      snprintf(fx.name, NAME_LEN, "Light %04X", fx.address);
    } else {
      // Cut on a character boundary: a UTF-8 continuation byte (10xxxxxx) at the cut
      // means the cut landed inside a character, so back up to its first byte.
      size_t n = strnlen(name, NAME_LEN);
      if (n > NAME_LEN - 1) {
        n = NAME_LEN - 1;
        while (n > 0 && (((uint8_t) name[n]) & 0xC0) == 0x80)
          n--;
      }
      if (n == 0) {  // nothing but continuation bytes: not a name
        snprintf(fx.name, NAME_LEN, "Light %04X", fx.address);
      } else {
        memcpy(fx.name, name, n);
        fx.name[n] = '\0';
      }
    }
    fx.caps.color = f["color"].as<bool>();
    fx.caps.gm = f["gm"].as<bool>();
    if ((!f["cct_min"].isNull() && !f["cct_min"].is<long>()) || (!f["cct_max"].isNull() && !f["cct_max"].is<long>()))
      return ParseError::BAD_CCT;
    long cmin = f["cct_min"].isNull() ? 2700 : f["cct_min"].as<long>();
    long cmax = f["cct_max"].isNull() ? 6500 : f["cct_max"].as<long>();
    if (cmin < 1000 || cmax > 20000 || cmin >= cmax)
      return ParseError::BAD_CCT;
    fx.caps.cct_min = (uint16_t) cmin;
    fx.caps.cct_max = (uint16_t) cmax;
    c.fixture_count++;
  }
  *out = c;
  return ParseError::OK;
}

const char *parse_error_text(ParseError e) {
  switch (e) {
    case ParseError::OK:
      return "ok";
    case ParseError::BAD_JSON:
      return "payload is not valid JSON";
    case ParseError::BAD_VERSION:
      return "payload version is not 1";
    case ParseError::BAD_KEY:
      return "net_key or app_key is not 16 bytes of hex";
    case ParseError::BAD_ADDRESS:
      return "a fixture address is not a unicast address (and own_address is never imported)";
    case ParseError::DUPLICATE_ADDRESS:
      return "two fixtures share a mesh address";
    case ParseError::BAD_MAC:
      return "a fixture MAC is malformed or duplicated";
    case ParseError::TOO_MANY:
      return "more than 32 fixtures";
    case ParseError::BAD_CCT:
      return "a fixture CCT range is invalid";
    case ParseError::EMPTY:
      return "no fixtures in payload";
    case ParseError::DUPLICATE_ID:
      return "two fixtures would share an entity id (same last three MAC bytes)";
  }
  return "unknown error";
}

uint16_t derive_own_address(uint16_t seed, const Fixture *fixtures, uint8_t count) {
  uint16_t addr = (uint16_t) (OWN_ADDRESS_BASE | (seed & OWN_ADDRESS_MASK));
  for (uint16_t tries = 0; tries <= count; tries++) {
    bool taken = false;
    for (uint8_t i = 0; i < count && !taken; i++)
      taken = fixtures[i].address == addr;
    if (!taken)
      return addr;
    addr = (uint16_t) (OWN_ADDRESS_BASE | ((addr + 1) & OWN_ADDRESS_MASK));
  }
  return addr;  // unreachable: at most `count` addresses can be taken
}

std::string object_id_for(const Fixture &f) {
  char buf[16];
  snprintf(buf, sizeof(buf), "amble_%02x%02x%02x", f.mac[3], f.mac[4], f.mac[5]);
  return buf;
}

}  // namespace amble
}  // namespace esphome
