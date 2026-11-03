#undef MINI_TEST_MAIN
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include "identity.h"
#include "mini_test.h"

using namespace esphome::amble;

namespace {
struct MemBackend : StoreBackend {
  std::map<std::string, std::vector<uint8_t>> kv;
  int commits = 0;
  bool fail_write = false;
  bool read(const char *key, void *buf, size_t *len) override {
    auto it = kv.find(key);
    if (it == kv.end() || it->second.size() > *len) return false;
    memcpy(buf, it->second.data(), it->second.size()); *len = it->second.size(); return true;
  }
  bool write(const char *key, const void *buf, size_t len) override {
    if (fail_write) return false;
    kv[key].assign((const uint8_t *) buf, (const uint8_t *) buf + len); return true;
  }
  bool probe_error = false;
  Probe probe(const char *key) override { return probe_error ? Probe::ERROR : kv.count(key) ? Probe::PRESENT : Probe::ABSENT; }
  bool erase(const char *key) override { kv.erase(key); return true; }
  bool commit() override { commits++; return true; }
};
using Load = IdentityHistory::Load;
const uint8_t NET_A[8] = {1, 2, 3, 4, 5, 6, 7, 8};
const uint8_t NET_B[8] = {9, 9, 9, 9, 9, 9, 9, 9};
}  // namespace

TEST(identity_resumes_a_used_identity_with_a_margin) {
  IdentityHistory h;
  uint32_t seq = 0;
  CHECK(!h.resume_seq(NET_A, 0x6CC0, &seq));
  h.remember(NET_A, 0x6CC0, 1000, 2);
  CHECK(h.resume_seq(NET_A, 0x6CC0, &seq)); CHECK_EQ(seq, 1000u + IdentityHistory::SEQ_MARGIN);
  CHECK(!h.resume_seq(NET_A, 0x6CC1, &seq));  // another address on the same network is fresh
  CHECK(!h.resume_seq(NET_B, 0x6CC0, &seq));  // the same address on another network is fresh
  CHECK_EQ(h.iv_for(NET_A), 2u); CHECK_EQ(h.iv_for(NET_B), 0u);
}
TEST(identity_never_lowers_and_iv_is_per_network) {
  IdentityHistory h;
  h.remember(NET_A, 0x6CC0, 500, 3);
  h.remember(NET_A, 0x6CC0, 200, 1);  // an older snapshot cannot roll back
  uint32_t seq = 0; CHECK(h.resume_seq(NET_A, 0x6CC0, &seq)); CHECK_EQ(seq, 500u + IdentityHistory::SEQ_MARGIN);
  h.remember(NET_A, 0x6CC1, 10, 7);   // the network's IV advanced while on another address
  CHECK_EQ(h.iv_for(NET_A), 7u);
  CHECK_EQ(h.size(), 2u);
}
TEST(identity_round_trips_through_the_store_and_tells_empty_from_unreadable) {
  MemBackend be; IdentityHistory h;
  CHECK((int) h.load(&be) == (int) Load::EMPTY);  // nothing stored yet: a fresh box
  CHECK(h.remember(NET_A, 0x6CC0, 42, 1)); CHECK(h.remember(NET_B, 0x0010, 7, 0));
  CHECK(h.save(&be)); CHECK_EQ(be.commits, 1);
  CHECK_EQ(be.kv["ident"].size(), 8u + 2 * sizeof(IdentityRecord));  // header + exactly the records held
  IdentityHistory g; CHECK((int) g.load(&be) == (int) Load::OK); CHECK_EQ(g.size(), 2u);
  uint32_t seq = 0; CHECK(g.resume_seq(NET_B, 0x0010, &seq)); CHECK_EQ(seq, 7u + IdentityHistory::SEQ_MARGIN);
  be.fail_write = true; CHECK(!h.save(&be)); be.fail_write = false;
  be.kv["ident"][0] ^= 0xFF;  // corrupt the magic: stored but untrustworthy, never "empty"
  IdentityHistory k; CHECK((int) k.load(&be) == (int) Load::UNREADABLE); CHECK_EQ(k.size(), 0u);
  be.kv["ident"].resize(5);   // truncated blob: also corrupt
  CHECK((int) k.load(&be) == (int) Load::UNREADABLE);
  be.kv["ident"].resize(4096, 0);  // a blob larger than ours (another layout): unreadable, not empty
  CHECK((int) k.load(&be) == (int) Load::UNREADABLE);
  be.kv.erase("ident"); be.probe_error = true;  // the store cannot even say: never "empty" (D1.2)
  CHECK((int) k.load(&be) == (int) Load::UNREADABLE);
  be.probe_error = false; CHECK((int) k.load(&be) == (int) Load::EMPTY);
  IdentityHistory full;  // a record at capacity survives the round trip whole
  for (uint16_t i = 0; i < IdentityHistory::MAX; i++) CHECK(full.remember(NET_A, (uint16_t) (0x4000 + i), i, 0));
  CHECK(full.save(&be)); IdentityHistory back; CHECK((int) back.load(&be) == (int) Load::OK); CHECK_EQ(back.size(), IdentityHistory::MAX);
}
TEST(identity_never_forgets_when_full) {
  IdentityHistory h;
  for (uint16_t i = 0; i < IdentityHistory::MAX; i++) CHECK(h.remember(NET_A, (uint16_t) (0x4000 + i), i, 0));
  CHECK_EQ(h.size(), IdentityHistory::MAX);
  CHECK(!h.remember(NET_B, 0x4000, 1, 0));  // a new identity is refused
  CHECK(h.remember(NET_A, 0x4000, 99, 0));  // a known one is still updated
  uint32_t seq = 0;
  CHECK(h.resume_seq(NET_A, 0x4000, &seq)); CHECK_EQ(seq, 99u + IdentityHistory::SEQ_MARGIN);
  CHECK_EQ(h.size(), IdentityHistory::MAX);
}
