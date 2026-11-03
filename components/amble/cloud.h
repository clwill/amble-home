#pragma once
// The cloud sign-in (spec amendment 2026-10-29): the box makes the one HTTPS login
// call to amaran's cloud on behalf of the Edit page, because a browser cannot send
// the two headers amaran requires. Everything here is pure and host-tested except
// cloud_login_call(), the device-only HTTPS worker. Nothing here ever logs, stores
// or returns an email, password or token except where the type says so.
#include <cstddef>
#include <cstdint>
#include <string>

namespace esphome {
namespace amble {

constexpr size_t CLOUD_EMAIL_MAX = 128;
constexpr size_t CLOUD_PW_MAX = 128;
constexpr const char *CLOUD_BUSY = "a sign-in is already running";
constexpr uint32_t CLOUD_SAVED_MAGIC = 0x414D4343;  // "AMCC"

struct CloudJob {
  char email[CLOUD_EMAIL_MAX + 1];
  char password[CLOUD_PW_MAX + 1];
  char region[3];
  bool remember;
  bool use_saved;
  void wipe();  // zeroes every byte through a volatile pointer (the compiler may not elide it)
};

// Stored in NVS under key "cloud" (namespace "amble") only when the person checked Remember.
struct CloudSaved {
  uint32_t magic;
  char email[CLOUD_EMAIL_MAX + 1];
  char password[CLOUD_PW_MAX + 1];
  char region[3];
  void wipe();
};

enum class CloudStage : uint8_t { NONE, TRANSPORT, HTTP, JSON, CODE, TOKEN };
const char *cloud_stage_text(CloudStage stage);

struct CloudOutcome {
  bool ok{false};
  CloudStage stage{CloudStage::NONE};
  int status{0};
  int code{0};
  std::string token;
};

bool cloud_region_valid(const char *region);  // "us" | "eu" | "cn" exactly
std::string cloud_login_url(const char *region);
const char *cloud_area(const char *region);   // "US" | "EU" | "CN" ("" for an invalid region)
std::string cloud_email_hint(const char *email);  // "c…@example.com"; no '@' (or nothing before it) -> "…"

// Classify amaran's reply. non-2xx -> HTTP; not one grammatical JSON object (strictly checked: commas,
// colons, matching closers, nothing after the closing brace), or no integer top-level "code" -> JSON;
// code != 20000 -> CODE; code 20000 without a non-empty string "token" -> TOKEN; else ok + token.
// truncated: the worker stopped reading at its buffer cap. Only then may the reply end early, and only
// after a complete code and token; an early end without truncated is JSON.
CloudOutcome cloud_classify(int status, const char *body, size_t len, bool truncated);

// Whether the worker may queue a finished sign-in for storage: the sign-in succeeded, the job asked to
// remember it (a fresh login, not the saved one), and no Forget arrived while it ran.
inline bool cloud_should_save(bool ok, bool remember_at_start, bool forgotten_meanwhile) {
  return ok && remember_at_start && !forgotten_meanwhile;
}

// The last storage operation, as the page sees it in the idle GET reply ("store").
enum class CloudStore : uint8_t { OK, PENDING, SAVE_FAILED, FORGET_FAILED };
const char *cloud_store_text(CloudStore store);  // "ok" | "pending" | "save failed" | "forget failed"

// Value type, guarded by its owner's mutex.
class CloudState {
 public:
  enum class Phase : uint8_t { IDLE, BUSY, DONE, FAILED };
  void start() { this->clear_outcome_(); this->phase_ = Phase::BUSY; }
  void finish(const CloudOutcome &o) { this->clear_outcome_(); this->outcome_ = o; this->phase_ = o.ok ? Phase::DONE : Phase::FAILED; }
  Phase phase() const { return this->phase_; }
  // Returns the outcome and resets to IDLE: a token is served once.
  CloudOutcome take_result();

 protected:
  void clear_outcome_();  // wipes the token before the buffer is released
  Phase phase_{Phase::IDLE};
  CloudOutcome outcome_;
};

// The login request body {"email":"..","password":"..","area":".."}: each field at most 128 bytes, all
// escaped worst case 6x. cloud_build_body writes it into buf (no terminator) and sets *len; false if it
// does not fit. append_escaped adds one JSON string body (no quotes) and is exposed for the host test.
constexpr size_t CLOUD_BODY_CAP = 2 * (CLOUD_EMAIL_MAX + CLOUD_PW_MAX) * 6 / 2 + 96;
bool append_escaped(char *buf, size_t cap, size_t *pos, const char *s);
bool cloud_build_body(const CloudJob &job, char *buf, size_t cap, size_t *len);

// The GET reply for one phase. Idle: email_hint_or_null is the already-hinted address (null -> saved:null),
// plus "store". Done carries the token; Failed carries stage/status/code. Hint and token are JSON-escaped.
std::string cloud_result_text(CloudState::Phase phase, const CloudOutcome &outcome, const char *email_hint_or_null,
                              const char *region_or_null, CloudStore store);

// A heap that zeroes every block it releases (the sign-in body's JSON parse runs on it, so no password
// copy outlives the parse). Each block carries its physical size in a header. A shrink or same-size
// reallocation keeps the block and returns the same pointer (it cannot fail; ArduinoJson relies on that),
// and the whole physical block is still zeroed at release. Growth allocates a new block, copies, zeroes
// and frees the old one; if that allocation fails it returns null and the old block stays as it was.
// raw_alloc/raw_free are the underlying allocator (heap_caps on the device, a fake in the host test).
struct WipeHeap {
  void *(*raw_alloc)(size_t size);
  void (*raw_free)(void *ptr);
  void *allocate(size_t size) const;
  void deallocate(void *ptr) const;
  void *reallocate(void *ptr, size_t new_size) const;
  static size_t capacity(const void *ptr);  // the physical size recorded for a block
};

#if defined(AMBLE_CLOUD_DOOR) && !defined(AMBLE_HOST_TEST)
// Device only: one HTTPS login (call from the worker task), bounded by a 20 s deadline. Wipes the request
// body and reply buffers it made before returning. Never logs anything variable except the region.
CloudOutcome cloud_login_call(const CloudJob &job);
#endif

}  // namespace amble
}  // namespace esphome
