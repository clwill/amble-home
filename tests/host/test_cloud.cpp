#undef MINI_TEST_MAIN
#include <cstdlib>
#include <cstring>
#include <utility>
#include <vector>

#include "cloud.h"
#include "mini_test.h"

using namespace esphome::amble;

static CloudOutcome cls(int status, const char *body) { return cloud_classify(status, body, strlen(body), false); }
static CloudOutcome cut(int status, const char *body) { return cloud_classify(status, body, strlen(body), true); }

TEST(cloud_region_valid_exact) {
  CHECK(cloud_region_valid("us")); CHECK(cloud_region_valid("eu")); CHECK(cloud_region_valid("cn"));
  CHECK(!cloud_region_valid("gs")); CHECK(!cloud_region_valid("US")); CHECK(!cloud_region_valid(""));
  CHECK(!cloud_region_valid(nullptr)); CHECK(!cloud_region_valid("usa"));
}
TEST(cloud_url_and_area) {
  CHECK(cloud_login_url("eu") == "https://eu.sidus.link/uc/user/login");
  CHECK(std::string(cloud_area("cn")) == "CN"); CHECK(std::string(cloud_area("us")) == "US");
  CHECK(std::string(cloud_area("eu")) == "EU");
}
TEST(cloud_hint) {
  CHECK(cloud_email_hint("chris@example.com") == "c\xE2\x80\xA6@example.com");
  CHECK(cloud_email_hint("nope") == "\xE2\x80\xA6");
  CHECK(cloud_email_hint("") == "\xE2\x80\xA6");
  CHECK(cloud_email_hint("@example.com") == "\xE2\x80\xA6");
}
TEST(cloud_stage_text_names) {
  CHECK(std::string(cloud_stage_text(CloudStage::TRANSPORT)) == "transport");
  CHECK(std::string(cloud_stage_text(CloudStage::HTTP)) == "http");
  CHECK(std::string(cloud_stage_text(CloudStage::JSON)) == "json");
  CHECK(std::string(cloud_stage_text(CloudStage::CODE)) == "code");
  CHECK(std::string(cloud_stage_text(CloudStage::TOKEN)) == "token");
}
TEST(cloud_classify_http) {
  CloudOutcome o = cloud_classify(502, "", 0, false);
  CHECK(!o.ok); CHECK(o.stage == CloudStage::HTTP); CHECK_EQ(o.status, 502);
  CHECK(cls(404, "{\"code\":20000}").stage == CloudStage::HTTP);
}
TEST(cloud_classify_json) {
  CHECK(cls(200, "not json").stage == CloudStage::JSON);
  CHECK(cls(200, "").stage == CloudStage::JSON);
  CHECK(cls(200, "[1,2]").stage == CloudStage::JSON);
  CHECK(cls(200, "{\"code\":\"20000\"}").stage == CloudStage::JSON);
  CHECK(cls(200, "{\"msg\":\"x\"}").stage == CloudStage::JSON);
  CHECK(cls(200, "{\"code\":2.5}").stage == CloudStage::JSON);
  CHECK(cls(200, "{\"data\":{\"code\":20000}}").stage == CloudStage::JSON);  // nested code is not the top-level one
}
TEST(cloud_classify_code) {
  CloudOutcome o = cls(200, "{\"code\":20014,\"msg\":\"x\"}");
  CHECK(o.stage == CloudStage::CODE); CHECK_EQ(o.code, 20014); CHECK_EQ(o.status, 200);
  o = cls(200, "{\"code\":10001}");
  CHECK(o.stage == CloudStage::CODE); CHECK_EQ(o.code, 10001);
  o = cls(200, "{ \"msg\" : \"a \\\"code\\\": 5\", \"code\" : 10001 }");
  CHECK(o.stage == CloudStage::CODE); CHECK_EQ(o.code, 10001);
}
TEST(cloud_classify_token) {
  CHECK(cls(200, "{\"code\":20000,\"data\":{}}").stage == CloudStage::TOKEN);
  CHECK(cls(200, "{\"code\":20000,\"data\":{\"token\":\"\"}}").stage == CloudStage::TOKEN);
  CHECK(cls(200, "{\"code\":20000,\"data\":{\"token\":5}}").stage == CloudStage::TOKEN);
  CloudOutcome o = cls(200, "{\"code\":20000,\"data\":{\"token\":\"abc\"}}");
  CHECK(o.ok); CHECK(o.token == "abc"); CHECK_EQ(o.code, 20000); CHECK_EQ(o.status, 200);
  o = cls(200, "{\"data\":{\"token\":\"abc\"},\"code\":20000}");
  CHECK(o.ok); CHECK(o.token == "abc");
}
TEST(cloud_classify_truncated_reply_after_token_is_ok_only_when_cut_at_the_cap) {
  const char *after = "{\"code\":20000,\"data\":{\"token\":\"abc\",\"user_info\":{\"name\":\"x\"},\"device_list\":[{\"a\":\"b";
  CloudOutcome o = cut(200, after);
  CHECK(o.ok); CHECK(o.token == "abc");
  o = cls(200, after);  // the same early end without the cap: not a complete reply
  CHECK(!o.ok); CHECK(o.stage == CloudStage::JSON); CHECK(o.token.empty());
  o = cut(200, "{\"code\":20000,\"data\":{\"token\":\"abc");  // cut inside the token: not complete
  CHECK(!o.ok); CHECK(o.stage == CloudStage::JSON);
  o = cut(200, "{\"code\":20000,\"da");
  CHECK(!o.ok); CHECK(o.stage == CloudStage::JSON);
  o = cut(200, "{\"data\":{\"token\":\"abc\"},\"co");  // no code seen
  CHECK(!o.ok); CHECK(o.stage == CloudStage::JSON);
  o = cut(200, "{\"code\":20000,\"data\":{\"token\":\"abc\"},\"x\":[1 2");  // ungrammatical before the cut
  CHECK(!o.ok); CHECK(o.stage == CloudStage::JSON);
}
TEST(cloud_classify_rejects_ungrammatical_json) {
  CHECK(cls(200, "{\"code\":20000 \"data\":{\"token\":\"abc\"}}").stage == CloudStage::JSON);  // missing comma
  CHECK(cut(200, "{\"code\":20000 \"data\":{\"token\":\"abc\"}}").stage == CloudStage::JSON);
  CHECK(cls(200, "{\"code\":20000,\"data\":{\"token\":\"abc\"]}").stage == CloudStage::JSON);  // mismatched closer
  CHECK(cls(200, "{\"code\":20000,\"data\":[\"token\",\"abc\"}}").stage == CloudStage::JSON);
  CHECK(cls(200, "{\"code\":20000,\"data\":{\"token\":\"abc\"}}x").stage == CloudStage::JSON);  // trailing garbage
  CHECK(cls(200, "{\"code\":20000,\"data\":{\"token\":\"abc\"}}}").stage == CloudStage::JSON);
  CHECK(cls(200, "{\"code\":20000,\"data\":{\"token\":\"abc\"}}{}").stage == CloudStage::JSON);
  CHECK(cls(200, "{\"code\" 20000,\"data\":{\"token\":\"abc\"}}").stage == CloudStage::JSON);  // missing colon
  CHECK(cls(200, "{\"code\":20000,,\"data\":{\"token\":\"abc\"}}").stage == CloudStage::JSON);  // double comma
  CHECK(cls(200, "{\"code\":20000,\"data\":{\"token\":\"abc\",}}").stage == CloudStage::JSON);  // trailing comma
  CHECK(cls(200, "{\"code\":20000,\"data\":{\"token\":\"abc\"},\"x\":tru}").stage == CloudStage::JSON);  // bad literal
  CHECK(cls(200, "{\"code\":20000,\"data\":{\"token\":\"abc\"},\"x\":01}").stage == CloudStage::JSON);  // leading zero
  CHECK(cls(200, "{\"code\":20000,\"data\":{\"token\":\"a\\qb\"}}").stage == CloudStage::JSON);  // invalid escape
  CHECK(cls(200, "{\"code\":20000,\"data\":{\"token\":\"a\tb\"}}").stage == CloudStage::JSON);  // raw control char
  CHECK(cls(200, "{code:20000}").stage == CloudStage::JSON);  // unquoted key
  // 17 levels deep is refused; 16 are fine.
  std::string deep = "{\"code\":20000,\"data\":{\"token\":\"abc\"},\"x\":";
  for (int k = 0; k < 15; k++) deep += "[";
  for (int k = 0; k < 15; k++) deep += "]";
  CHECK(cls(200, (deep + "}").c_str()).ok);
  deep.insert(deep.find(":[") + 1, "[");
  deep += "]";
  CHECK(cls(200, (deep + "}").c_str()).stage == CloudStage::JSON);
}
TEST(cloud_classify_accepts_grammatical_json_of_every_kind) {
  CloudOutcome o = cls(200, " { \"code\" : 20000 , \"ok\":true, \"no\":false, \"n\":null, \"f\":-1.5e+3, \"a\":[ ], \"o\":{ },"
                            " \"data\" : { \"x\":[1,{\"y\":\"\\u00e9\\n\"}], \"token\" : \"abc\" } } \r\n");
  CHECK(o.ok); CHECK(o.token == "abc");
}
TEST(cloud_classify_token_only_from_data) {
  CloudOutcome o = cls(200, "{\"code\":20000,\"user_info\":{\"token\":\"decoy\"},\"data\":{\"token\":\"real\"}}");
  CHECK(o.ok); CHECK(o.token == "real");
  o = cls(200, "{\"code\":20000,\"data\":{\"user_info\":{\"token\":\"decoy\"},\"token\":\"real\"}}");
  CHECK(o.ok); CHECK(o.token == "real");
  o = cls(200, "{\"code\":20000,\"user_info\":{\"token\":\"decoy\"}}");
  CHECK(o.stage == CloudStage::TOKEN);
  o = cls(200, "{\"code\":20000,\"token\":\"top\"}");  // top level is not data
  CHECK(o.stage == CloudStage::TOKEN);
}
TEST(cloud_classify_token_with_odd_escape_fails_closed) {
  CHECK(cls(200, "{\"code\":20000,\"data\":{\"token\":\"a\\u0041b\"}}").stage == CloudStage::TOKEN);
  CloudOutcome o = cls(200, "{\"code\":20000,\"data\":{\"token\":\"a\\/b\\\"c\\\\\"}}");
  CHECK(o.ok); CHECK(o.token == "a/b\"c\\");
}
TEST(cloud_state_lifecycle) {
  CloudState s; CHECK(s.phase() == CloudState::Phase::IDLE);
  s.start(); CHECK(s.phase() == CloudState::Phase::BUSY);
  CloudOutcome ok; ok.ok = true; ok.stage = CloudStage::NONE; ok.status = 200; ok.code = 20000; ok.token = "abc";
  s.finish(ok); CHECK(s.phase() == CloudState::Phase::DONE);
  CloudOutcome r = s.take_result();
  CHECK(r.ok); CHECK(r.token == "abc"); CHECK(s.phase() == CloudState::Phase::IDLE);
  r = s.take_result(); CHECK(!r.ok); CHECK(r.stage == CloudStage::NONE); CHECK(r.token.empty());
}
TEST(cloud_state_failed_is_read_once_too) {
  CloudState s; s.start();
  CloudOutcome bad; bad.ok = false; bad.stage = CloudStage::CODE; bad.status = 200; bad.code = 20014;
  s.finish(bad); CHECK(s.phase() == CloudState::Phase::FAILED);
  CloudOutcome r = s.take_result(); CHECK(r.stage == CloudStage::CODE); CHECK_EQ(r.code, 20014);
  CHECK(s.phase() == CloudState::Phase::IDLE);
}
TEST(cloud_job_wipe_zeroes_every_byte) {
  CloudJob j;
  memset(&j, 0xA5, sizeof(j));
  j.wipe();
  const unsigned char *p = reinterpret_cast<const unsigned char *>(&j);
  bool all = true;
  for (size_t i = 0; i < sizeof(j); i++) if (p[i] != 0) all = false;
  CHECK(all);
}

// ---- result JSON and the request body (cloud.cpp, pure) ----
static CloudOutcome outcome_of(CloudStage st, int status, int code, const char *tok = "") {
  CloudOutcome o; o.stage = st; o.status = status; o.code = code; o.token = tok; return o;
}
TEST(cloud_result_text_idle_with_hint) {
  CHECK(cloud_result_text(CloudState::Phase::IDLE, CloudOutcome{}, "c\xE2\x80\xA6@example.com", "us", CloudStore::OK) ==
        "{\"state\":\"idle\",\"saved\":{\"email_hint\":\"c\xE2\x80\xA6@example.com\",\"region\":\"us\"},\"store\":\"ok\"}");
  CHECK(cloud_result_text(CloudState::Phase::IDLE, CloudOutcome{}, "a\"b\\c@x", "eu", CloudStore::PENDING) ==
        "{\"state\":\"idle\",\"saved\":{\"email_hint\":\"a\\\"b\\\\c@x\",\"region\":\"eu\"},\"store\":\"pending\"}");
}
TEST(cloud_result_text_idle_without_hint) {
  CHECK(cloud_result_text(CloudState::Phase::IDLE, CloudOutcome{}, nullptr, nullptr, CloudStore::OK) ==
        "{\"state\":\"idle\",\"saved\":null,\"store\":\"ok\"}");
  CHECK(cloud_result_text(CloudState::Phase::IDLE, CloudOutcome{}, nullptr, nullptr, CloudStore::SAVE_FAILED) ==
        "{\"state\":\"idle\",\"saved\":null,\"store\":\"save failed\"}");
  CHECK(cloud_result_text(CloudState::Phase::IDLE, CloudOutcome{}, "c\xE2\x80\xA6@x", "us", CloudStore::FORGET_FAILED) ==
        "{\"state\":\"idle\",\"saved\":{\"email_hint\":\"c\xE2\x80\xA6@x\",\"region\":\"us\"},\"store\":\"forget failed\"}");
}
TEST(cloud_result_text_busy) {
  CHECK(cloud_result_text(CloudState::Phase::BUSY, CloudOutcome{}, nullptr, nullptr, CloudStore::OK) == "{\"state\":\"busy\"}");
}
TEST(cloud_result_text_done_carries_token) {
  CHECK(cloud_result_text(CloudState::Phase::DONE, outcome_of(CloudStage::NONE, 200, 20000, "tok\"en"), nullptr, nullptr, CloudStore::OK) ==
        "{\"state\":\"done\",\"token\":\"tok\\\"en\"}");
}
TEST(cloud_result_text_failed_carries_stage_status_code) {
  CHECK(cloud_result_text(CloudState::Phase::FAILED, outcome_of(CloudStage::CODE, 200, 20014), nullptr, nullptr, CloudStore::OK) ==
        "{\"state\":\"failed\",\"stage\":\"code\",\"status\":200,\"code\":20014}");
  CHECK(cloud_result_text(CloudState::Phase::FAILED, outcome_of(CloudStage::TRANSPORT, 0, 0), nullptr, nullptr, CloudStore::OK) ==
        "{\"state\":\"failed\",\"stage\":\"transport\",\"status\":0,\"code\":0}");
}

// Undo the JSON escapes of one string body.
static std::string unescape(const std::string &e) {
  std::string o;
  for (size_t i = 0; i < e.size(); i++) {
    if (e[i] != '\\') { o += e[i]; continue; }
    char c = e[++i];
    if (c == 'u') { o += (char) std::stoi(e.substr(i + 1, 4), nullptr, 16); i += 4; }
    else o += c;
  }
  return o;
}
TEST(cloud_build_body_roundtrips_a_worst_case_password) {
  CloudJob j; memset(&j, 0, sizeof(j));
  strcpy(j.email, "someone@example.invalid"); strcpy(j.region, "us");
  // 128 bytes of ", \\, newline and a two-byte UTF-8 character (U+00E9), repeated.
  std::string pw;
  while (pw.size() + 5 <= CLOUD_PW_MAX) pw += std::string("\"") + "\\" + "\n" + "\xC3\xA9";
  while (pw.size() < CLOUD_PW_MAX) pw += '"';
  CHECK_EQ(pw.size(), CLOUD_PW_MAX);
  strcpy(j.password, pw.c_str());
  std::string buf(CLOUD_BODY_CAP, '\0'); size_t len = 0;
  CHECK(cloud_build_body(j, &buf[0], CLOUD_BODY_CAP, &len));
  CHECK(len < CLOUD_BODY_CAP);
  std::string body = buf.substr(0, len);
  const std::string a = "{\"email\":\"someone@example.invalid\",\"password\":\"", z = "\",\"area\":\"US\"}";
  CHECK(body.compare(0, a.size(), a) == 0);
  CHECK(body.size() > z.size() && body.compare(body.size() - z.size(), z.size(), z) == 0);
  CHECK(unescape(body.substr(a.size(), body.size() - a.size() - z.size())) == pw);
}
TEST(cloud_build_body_refuses_a_buffer_that_is_too_small) {
  CloudJob j; memset(&j, 0, sizeof(j));
  strcpy(j.email, "a@b"); strcpy(j.password, "pw"); strcpy(j.region, "eu");
  char small[16]; size_t len = 0;
  CHECK(!cloud_build_body(j, small, sizeof(small), &len));
}
TEST(cloud_store_text_names) {
  CHECK(std::string(cloud_store_text(CloudStore::OK)) == "ok");
  CHECK(std::string(cloud_store_text(CloudStore::PENDING)) == "pending");
  CHECK(std::string(cloud_store_text(CloudStore::SAVE_FAILED)) == "save failed");
  CHECK(std::string(cloud_store_text(CloudStore::FORGET_FAILED)) == "forget failed");
}
// Forget during an in-flight remembered sign-in: the worker decides at finish time, so a Forget pressed
// meanwhile wins, whatever the order of finish and the main loop's erase.
TEST(cloud_should_save_only_a_remembered_fresh_sign_in_not_forgotten) {
  CHECK(cloud_should_save(true, true, false));
  CHECK(!cloud_should_save(true, true, true));    // Forget pressed while it ran
  CHECK(!cloud_should_save(false, true, false));  // the sign-in failed
  CHECK(!cloud_should_save(true, false, false));  // not asked to remember (or the saved sign-in itself)
  CHECK(!cloud_should_save(false, false, true));
}

// ---- the wiping heap (cloud.cpp, pure) ----
namespace {
struct FakeRaw {
  static int allocs, frees, fail_next;
  static bool last_free_all_zero;
  static size_t last_free_len;
  static std::vector<std::pair<void *, size_t>> live;
  static void *alloc(size_t n) {
    if (fail_next > 0) { fail_next--; return nullptr; }
    allocs++;
    void *p = malloc(n);
    memset(p, 0xA5, n);
    live.push_back({p, n});
    return p;
  }
  static void free_(void *p) {
    frees++;
    for (auto it = live.begin(); it != live.end(); ++it) {
      if (it->first != p) continue;
      const unsigned char *b = static_cast<const unsigned char *>(p);
      last_free_all_zero = true;
      for (size_t i = 0; i < it->second; i++) if (b[i] != 0) last_free_all_zero = false;
      last_free_len = it->second;
      live.erase(it);
      break;
    }
    free(p);
  }
  static void reset() { allocs = frees = fail_next = 0; last_free_all_zero = false; last_free_len = 0; live.clear(); }
};
int FakeRaw::allocs, FakeRaw::frees, FakeRaw::fail_next;
bool FakeRaw::last_free_all_zero;
size_t FakeRaw::last_free_len;
std::vector<std::pair<void *, size_t>> FakeRaw::live;
const WipeHeap FAKE{&FakeRaw::alloc, &FakeRaw::free_};
}  // namespace

TEST(wipe_heap_release_zeroes_the_whole_block) {
  FakeRaw::reset();
  char *p = static_cast<char *>(FAKE.allocate(32));
  CHECK(p != nullptr); CHECK_EQ(WipeHeap::capacity(p), (size_t) 32);
  memcpy(p, "not-a-real-password-not-a-real-p", 32);
  FAKE.deallocate(p);
  CHECK_EQ(FakeRaw::frees, 1); CHECK(FakeRaw::last_free_all_zero); CHECK(FakeRaw::last_free_len >= 32);
  FAKE.deallocate(nullptr);
  CHECK_EQ(FakeRaw::frees, 1);
}
TEST(wipe_heap_shrink_keeps_the_block_and_cannot_fail) {
  FakeRaw::reset();
  char *p = static_cast<char *>(FAKE.allocate(64));
  memset(p, 'x', 64);
  FakeRaw::fail_next = 5;  // any allocation would fail now
  CHECK(FAKE.reallocate(p, 16) == p);
  CHECK(FAKE.reallocate(p, 64) == p);  // back up to the recorded size: still the same block
  CHECK_EQ(WipeHeap::capacity(p), (size_t) 64);
  CHECK_EQ(FakeRaw::allocs, 1); CHECK_EQ(FakeRaw::frees, 0);
  FakeRaw::fail_next = 0;
  FAKE.deallocate(p);
  CHECK(FakeRaw::last_free_all_zero); CHECK(FakeRaw::last_free_len >= 64);  // all 64 bytes, not 16
}
TEST(wipe_heap_growth_copies_and_zeroes_the_old_block) {
  FakeRaw::reset();
  char *p = static_cast<char *>(FAKE.allocate(8));
  memcpy(p, "abcdefgh", 8);
  char *q = static_cast<char *>(FAKE.reallocate(p, 24));
  CHECK(q != nullptr); CHECK(q != p); CHECK(memcmp(q, "abcdefgh", 8) == 0);
  CHECK_EQ(FakeRaw::frees, 1); CHECK(FakeRaw::last_free_all_zero);
  FakeRaw::fail_next = 1;
  CHECK(FAKE.reallocate(q, 4096) == nullptr);  // growth failure: null, old block untouched
  CHECK(memcmp(q, "abcdefgh", 8) == 0); CHECK_EQ(FakeRaw::frees, 1);
  FAKE.deallocate(q);
  CHECK(FakeRaw::last_free_all_zero); CHECK(FakeRaw::live.empty());
  void *r = FAKE.reallocate(nullptr, 4);  // null grows like allocate
  CHECK(r != nullptr); FAKE.deallocate(r);
  FakeRaw::reset();
}
