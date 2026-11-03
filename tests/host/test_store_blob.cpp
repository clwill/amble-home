#undef MINI_TEST_MAIN
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include "mini_test.h"
#include "store.h"

using namespace esphome::amble;

struct MapBackend : StoreBackend {
  std::map<std::string, std::vector<uint8_t>> kv;
  std::string fail_write_key;  // simulate power loss: this write "never happens"
  int writes = 0, commits = 0;
  bool read(const char *key, void *buf, size_t *len) override {
    auto it = kv.find(key);
    if (it == kv.end() || it->second.size() > *len) return false;
    memcpy(buf, it->second.data(), it->second.size()); *len = it->second.size(); return true;
  }
  bool write(const char *key, const void *buf, size_t len) override {
    writes++;
    if (fail_write_key == key) return false;
    auto *p = static_cast<const uint8_t *>(buf); kv[key] = std::vector<uint8_t>(p, p + len); return true;
  }
  bool probe_error = false;
  Probe probe(const char *key) override { return probe_error ? Probe::ERROR : kv.count(key) ? Probe::PRESENT : Probe::ABSENT; }
  bool erase(const char *key) override { kv.erase(key); return true; }
  bool commit() override { commits++; return true; }
};

static Config sample(uint8_t n, uint8_t seed) {
  Config c{}; for (int i = 0; i < 16; i++) { c.net_key[i] = (uint8_t) (seed + i); c.app_key[i] = (uint8_t) (0xF0 - i); }
  c.own_address = 0x72C3; c.fixture_count = n;
  for (uint8_t i = 0; i < n; i++) { c.fixtures[i].address = 0x0010 + i; c.fixtures[i].mac[5] = i; snprintf(c.fixtures[i].name, NAME_LEN, "L%u", i); c.fixtures[i].caps.color = true; }
  return c;
}

TEST(load_on_empty_store_is_false) { MapBackend b; ConfigStore s(&b); Config c; CHECK(!s.load(&c)); }
TEST(commit_then_load_roundtrips_config) {
  MapBackend b; ConfigStore s(&b); Config in = sample(2, 1), out{};
  CHECK(s.commit(in)); CHECK(s.load(&out)); CHECK(memcmp(&in, &out, sizeof(Config)) == 0);
  CHECK_EQ(out.fixture_count, 2); CHECK_EQ(out.fixtures[1].address, 0x0011); CHECK(strcmp(out.fixtures[1].name, "L1") == 0);
}
TEST(second_commit_goes_to_other_slot_and_flips) {
  MapBackend b; ConfigStore s(&b);
  CHECK(s.commit(sample(1, 1))); CHECK_EQ(b.kv["active"][0], 0); CHECK(b.kv.count("cfg0") == 1); CHECK(b.kv.count("cfg1") == 0);
  CHECK(s.commit(sample(3, 2))); CHECK_EQ(b.kv["active"][0], 1); CHECK(b.kv.count("cfg1") == 1);
  Config out{}; CHECK(s.load(&out)); CHECK_EQ(out.fixture_count, 3);
  CHECK(s.commit(sample(4, 3))); CHECK_EQ(b.kv["active"][0], 0); CHECK(s.load(&out)); CHECK_EQ(out.fixture_count, 4);
}
TEST(commit_is_atomic_when_pointer_not_flipped) {
  MapBackend b; ConfigStore s(&b); CHECK(s.commit(sample(1, 1)));
  b.fail_write_key = "active";                 // power lost before the pointer flips
  CHECK(!s.commit(sample(5, 9)));
  Config out{}; CHECK(s.load(&out)); CHECK_EQ(out.fixture_count, 1); CHECK_EQ(out.net_key[0], 1);
}
TEST(commit_fails_when_slot_write_fails_and_old_config_survives) {
  MapBackend b; ConfigStore s(&b); CHECK(s.commit(sample(1, 1)));
  b.fail_write_key = "cfg1"; CHECK(!s.commit(sample(2, 2)));
  Config out{}; CHECK(s.load(&out)); CHECK_EQ(out.fixture_count, 1);
}
TEST(corrupt_active_slot_falls_back_to_other) {
  MapBackend b; ConfigStore s(&b); CHECK(s.commit(sample(1, 1))); CHECK(s.commit(sample(2, 2)));  // active=1
  b.kv["cfg1"][40] ^= 0xFF;                      // flip a byte inside the active blob
  Config out{}; CHECK(s.load(&out)); CHECK_EQ(out.fixture_count, 1);   // the previous config
  b.kv["cfg0"][40] ^= 0xFF; CHECK(!s.load(&out));                     // both corrupt → nothing, never garbage
}
TEST(wrong_magic_or_version_is_absent) {
  MapBackend b; ConfigStore s(&b); CHECK(s.commit(sample(1, 1)));
  b.kv["cfg0"][0] ^= 1; Config out{}; CHECK(!s.load(&out));
}
TEST(fixture_count_over_max_is_rejected_even_with_valid_crc) {
  MapBackend b; ConfigStore s(&b); Config c = sample(1, 1); c.fixture_count = 40;
  s.commit(c);  // the store does not police the count on write…
  Config out{}; CHECK(!s.load(&out));  // …but load refuses an impossible one, never returning garbage
}
TEST(commit_after_fallback_recovery_does_not_overwrite_the_recovered_slot) {
  MapBackend b; ConfigStore s(&b); CHECK(s.commit(sample(1, 1))); CHECK(s.commit(sample(2, 2)));  // active=1
  b.kv["cfg1"][40] ^= 0xFF;                      // active slot corrupt; cfg0 (1 fixture) is the live fallback
  Config out{}; CHECK(s.load(&out)); CHECK_EQ(out.fixture_count, 1);
  b.fail_write_key = "active";                   // and now the pointer write fails during the next commit
  CHECK(!s.commit(sample(7, 7)));
  CHECK(s.load(&out)); CHECK_EQ(out.fixture_count, 1);  // still the recovered configuration, not the new one
  b.fail_write_key = "";
  CHECK(s.commit(sample(7, 7))); CHECK(s.load(&out)); CHECK_EQ(out.fixture_count, 7);
}
TEST(clear_removes_everything) {
  MapBackend b; ConfigStore s(&b); CHECK(s.commit(sample(1, 1))); CHECK(s.clear()); Config out{}; CHECK(!s.load(&out)); CHECK(b.kv.empty());
}
TEST(crc32_known_vector) { CHECK_EQ(ConfigStore::crc32("123456789", 9), 0xCBF43926u); }
