#include "cloud.h"

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <utility>

#if !defined(AMBLE_HOST_TEST)
#include "esphome/core/defines.h"
#endif
#if defined(AMBLE_CLOUD_DOOR) && !defined(AMBLE_HOST_TEST)
#include <esp_crt_bundle.h>
#include <esp_heap_caps.h>
#include <esp_http_client.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <sys/socket.h>

#include <mutex>
#endif

namespace esphome {
namespace amble {

static void wipe_bytes(void *p, size_t n) {
  volatile unsigned char *v = static_cast<volatile unsigned char *>(p);
  for (size_t i = 0; i < n; i++)
    v[i] = 0;
}

void CloudJob::wipe() { wipe_bytes(this, sizeof(*this)); }
void CloudSaved::wipe() { wipe_bytes(this, sizeof(*this)); }

const char *cloud_stage_text(CloudStage stage) {
  switch (stage) {
    case CloudStage::TRANSPORT: return "transport";
    case CloudStage::HTTP: return "http";
    case CloudStage::JSON: return "json";
    case CloudStage::CODE: return "code";
    case CloudStage::TOKEN: return "token";
    default: return "none";
  }
}

bool cloud_region_valid(const char *region) {
  return region != nullptr && (!strcmp(region, "us") || !strcmp(region, "eu") || !strcmp(region, "cn"));
}

std::string cloud_login_url(const char *region) {
  return std::string("https://") + region + ".sidus.link/uc/user/login";
}

const char *cloud_area(const char *region) {
  if (!cloud_region_valid(region))
    return "";
  return !strcmp(region, "us") ? "US" : !strcmp(region, "eu") ? "EU" : "CN";
}

std::string cloud_email_hint(const char *email) {
  static const char *const ELLIPSIS = "\xE2\x80\xA6";
  const char *at = email ? strchr(email, '@') : nullptr;
  if (at == nullptr || at == email)
    return ELLIPSIS;
  return std::string(email, 1) + ELLIPSIS + at;
}

void CloudState::clear_outcome_() {
  this->outcome_.token.assign(this->outcome_.token.size(), '\0');
  this->outcome_ = CloudOutcome();
}

CloudOutcome CloudState::take_result() {
  CloudOutcome out;
  if (this->phase_ == Phase::DONE || this->phase_ == Phase::FAILED)
    out = std::move(this->outcome_);  // the token's buffer moves to the caller: no copy is left behind
  this->clear_outcome_();
  this->phase_ = Phase::IDLE;
  return out;
}

// ---- the one minimal scanner, shared by host and device --------------------------------------
namespace {

static void wipe_str(std::string &t) { t.assign(t.size(), '\0'); }

// END: the input stopped inside the token (it may have been cut at the buffer cap). BAD: not JSON.
enum class Tok : uint8_t { OK, END, BAD };

struct Scan {
  const char *s;
  size_t n;
  size_t i{0};
  void ws() { while (i < n && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) i++; }
  static bool digit(char c) { return c >= '0' && c <= '9'; }
  static bool hex(char c) { return digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
  // At an opening quote: reads the string. Decodes \" \\ \/; any other valid escape sets *odd and is kept
  // verbatim. A raw control character or an invalid escape is BAD.
  Tok str(std::string *out, bool *odd) {
    i++;
    while (i < n) {
      unsigned char c = (unsigned char) s[i++];
      if (c == '"') return Tok::OK;
      if (c < 0x20) return Tok::BAD;
      if (c != '\\') { if (out) out->push_back((char) c); continue; }
      if (i >= n) return Tok::END;
      char e = s[i++];
      if (e == '"' || e == '\\' || e == '/') { if (out) out->push_back(e); continue; }
      bool simple = e == 'b' || e == 'f' || e == 'n' || e == 'r' || e == 't';
      if (!simple && e != 'u') return Tok::BAD;
      if (odd) *odd = true;
      if (out) { out->push_back('\\'); out->push_back(e); }
      if (e == 'u') {
        for (int k = 0; k < 4; k++) {
          if (i >= n) return Tok::END;
          if (!hex(s[i])) return Tok::BAD;
          if (out) out->push_back(s[i]);
          i++;
        }
      }
    }
    return Tok::END;
  }
  // A number per the JSON grammar. *integer: no fraction or exponent. *fits/*val: an integer of at most
  // nine digits and its value. A number running to the end of the input is END (more digits may follow).
  Tok number(bool *integer, bool *fits, long *val) {
    bool neg = s[i] == '-';
    if (neg) i++;
    if (i >= n) return Tok::END;
    size_t d0 = i;
    if (s[i] == '0') i++;
    else if (digit(s[i])) while (i < n && digit(s[i])) i++;
    else return Tok::BAD;
    size_t d1 = i;
    *integer = true;
    if (i < n && s[i] == '.') {
      *integer = false; i++;
      if (i >= n) return Tok::END;
      if (!digit(s[i])) return Tok::BAD;
      while (i < n && digit(s[i])) i++;
    }
    if (i < n && (s[i] == 'e' || s[i] == 'E')) {
      *integer = false; i++;
      if (i < n && (s[i] == '+' || s[i] == '-')) i++;
      if (i >= n) return Tok::END;
      if (!digit(s[i])) return Tok::BAD;
      while (i < n && digit(s[i])) i++;
    }
    if (i >= n) return Tok::END;
    *fits = *integer && d1 - d0 <= 9;
    long v = 0;
    if (*fits) for (size_t k = d0; k < d1; k++) v = v * 10 + (s[k] - '0');
    *val = neg ? -v : v;
    return Tok::OK;
  }
  Tok literal(const char *word) {
    for (; *word; word++, i++) {
      if (i >= n) return Tok::END;
      if (s[i] != *word) return Tok::BAD;
    }
    return Tok::OK;
  }
};

enum class ScanEnd : uint8_t { CLOSED, RAN_OUT, MALFORMED };

struct ScanResult {
  bool has_code{false}, code_bad{false}, has_token{false}, token_odd{false};
  int code{0};
  std::string token;
};

// Walks the body once and checks its grammar strictly: one object, keys then ':' then a value, values
// separated by ',', closers matching their openers (at most 16 deep), nothing but whitespace after the
// closing brace. code: the first top-level integer "code". token: the first string "token" directly
// inside the top-level object "data". RAN_OUT: the input ended inside the object; what was read before
// that point was grammatical.
ScanEnd scan(const char *body, size_t len, ScanResult *r) {
  enum class Want : uint8_t { VALUE, VALUE_OR_CLOSE, KEY, KEY_OR_CLOSE, COLON, COMMA_OR_CLOSE };
  static constexpr int MAX_DEPTH = 16;
  char open[MAX_DEPTH];      // '{' or '['
  bool is_data[MAX_DEPTH];   // that container is the object under the top-level key "data"
  int depth = 0;
  Scan sc{body, len};
  sc.ws();
  if (sc.i >= sc.n || sc.s[sc.i] != '{') return ScanEnd::MALFORMED;
  Want want = Want::VALUE;
  std::string key;  // the key whose value comes next, when the current container is an object
  while (true) {
    sc.ws();
    if (sc.i >= sc.n) return ScanEnd::RAN_OUT;
    char c = sc.s[sc.i];
    bool closer = (c == '}' && (want == Want::KEY_OR_CLOSE || want == Want::COMMA_OR_CLOSE)) ||
                  (c == ']' && (want == Want::VALUE_OR_CLOSE || want == Want::COMMA_OR_CLOSE));
    if (closer) {
      if (depth == 0 || open[depth - 1] != (c == '}' ? '{' : '[')) return ScanEnd::MALFORMED;
      depth--;
      sc.i++;
      if (depth == 0) {
        sc.ws();
        return sc.i == sc.n ? ScanEnd::CLOSED : ScanEnd::MALFORMED;
      }
      want = Want::COMMA_OR_CLOSE;
      continue;
    }
    switch (want) {
      case Want::COLON:
        if (c != ':') return ScanEnd::MALFORMED;
        sc.i++;
        want = Want::VALUE;
        continue;
      case Want::COMMA_OR_CLOSE:
        if (c != ',') return ScanEnd::MALFORMED;
        sc.i++;
        want = open[depth - 1] == '{' ? Want::KEY : Want::VALUE;
        continue;
      case Want::KEY:
      case Want::KEY_OR_CLOSE: {
        if (c != '"') return ScanEnd::MALFORMED;
        std::string k;
        Tok t = sc.str(&k, nullptr);
        if (t != Tok::OK) return t == Tok::END ? ScanEnd::RAN_OUT : ScanEnd::MALFORMED;
        key = std::move(k);
        want = Want::COLON;
        continue;
      }
      default:
        break;  // a value
    }
    bool keyed = depth > 0 && open[depth - 1] == '{';
    bool code_here = keyed && depth == 1 && key == "code" && !r->has_code && !r->code_bad;
    if (c == '{' || c == '[') {
      if (depth == MAX_DEPTH) return ScanEnd::MALFORMED;
      open[depth] = c;
      is_data[depth] = c == '{' && keyed && depth == 1 && key == "data";
      depth++;
      sc.i++;
      if (code_here) r->code_bad = true;
      want = c == '{' ? Want::KEY_OR_CLOSE : Want::VALUE_OR_CLOSE;
      continue;
    }
    Tok t;
    if (c == '"') {
      bool take = keyed && depth == 2 && is_data[1] && key == "token" && !r->has_token;
      std::string v;
      bool odd = false;
      t = sc.str(take ? &v : nullptr, &odd);
      if (t != Tok::OK) wipe_str(v);
      else if (take) { r->token = std::move(v); r->token_odd = odd; r->has_token = true; }
      if (code_here) r->code_bad = true;
    } else if (c == '-' || Scan::digit(c)) {
      bool integer = false, fits = false;
      long v = 0;
      t = sc.number(&integer, &fits, &v);
      if (t == Tok::OK && code_here) {
        if (fits) { r->has_code = true; r->code = (int) v; }
        else r->code_bad = true;
      }
    } else if (c == 't' || c == 'f' || c == 'n') {
      t = sc.literal(c == 't' ? "true" : c == 'f' ? "false" : "null");
      if (code_here) r->code_bad = true;
    } else {
      return ScanEnd::MALFORMED;
    }
    if (t != Tok::OK) return t == Tok::END ? ScanEnd::RAN_OUT : ScanEnd::MALFORMED;
    want = Want::COMMA_OR_CLOSE;
  }
}

}  // namespace

CloudOutcome cloud_classify(int status, const char *body, size_t len, bool truncated) {
  CloudOutcome o;
  o.status = status;
  if (status < 200 || status > 299) { o.stage = CloudStage::HTTP; return o; }
  ScanResult r;
  ScanEnd end = body ? scan(body, len, &r) : ScanEnd::MALFORMED;
  bool usable = end == ScanEnd::CLOSED || (end == ScanEnd::RAN_OUT && truncated && r.has_code && r.has_token);
  if (!usable || !r.has_code) {
    wipe_str(r.token);
    o.stage = CloudStage::JSON;
    return o;
  }
  o.code = r.code;
  if (r.code != 20000) { wipe_str(r.token); o.stage = CloudStage::CODE; return o; }
  if (!r.has_token || r.token.empty() || r.token_odd) { wipe_str(r.token); o.stage = CloudStage::TOKEN; return o; }
  o.ok = true;
  o.token = std::move(r.token);
  return o;
}

// ---- request body and result JSON (pure; host-tested) -----------------------------------------
// Appends s as a JSON string body (no surrounding quotes): escapes ", \ and control characters.
bool append_escaped(char *buf, size_t cap, size_t *pos, const char *s) {
  for (; *s; s++) {
    unsigned char c = (unsigned char) *s;
    char tmp[7];
    size_t k = 0;
    if (c == '"' || c == '\\') { tmp[k++] = '\\'; tmp[k++] = (char) c; }
    else if (c < 0x20) { k = (size_t) snprintf(tmp, sizeof(tmp), "\\u%04x", c); }
    else tmp[k++] = (char) c;
    if (*pos + k >= cap) return false;
    memcpy(buf + *pos, tmp, k);
    *pos += k;
  }
  return true;
}

bool cloud_build_body(const CloudJob &job, char *buf, size_t cap, size_t *len) {
  static const char *const P1 = "{\"email\":\"", *const P2 = "\",\"password\":\"", *const P3 = "\",\"area\":\"", *const P4 = "\"}";
  size_t pos = 0;
  auto lit = [&](const char *t) { size_t l = strlen(t); if (pos + l >= cap) return false; memcpy(buf + pos, t, l); pos += l; return true; };
  bool ok = lit(P1) && append_escaped(buf, cap, &pos, job.email) && lit(P2) &&
            append_escaped(buf, cap, &pos, job.password) && lit(P3) &&
            append_escaped(buf, cap, &pos, cloud_area(job.region)) && lit(P4);
  *len = pos;
  return ok;
}

static void json_escape_append(std::string &out, const char *s) {
  for (const unsigned char *p = (const unsigned char *) s; *p; p++) {
    if (*p == '"') out += "\\\"";
    else if (*p == '\\') out += "\\\\";
    else if (*p < 0x20) { char b[8]; snprintf(b, sizeof(b), "\\u%04x", *p); out += b; }
    else out += (char) *p;
  }
}

const char *cloud_store_text(CloudStore store) {
  switch (store) {
    case CloudStore::PENDING: return "pending";
    case CloudStore::SAVE_FAILED: return "save failed";
    case CloudStore::FORGET_FAILED: return "forget failed";
    default: return "ok";
  }
}

std::string cloud_result_text(CloudState::Phase phase, const CloudOutcome &outcome, const char *email_hint_or_null,
                              const char *region_or_null, CloudStore store) {
  switch (phase) {
    case CloudState::Phase::BUSY:
      return "{\"state\":\"busy\"}";
    case CloudState::Phase::DONE: {
      std::string s = "{\"state\":\"done\",\"token\":\"";
      json_escape_append(s, outcome.token.c_str());
      s += "\"}";
      return s;
    }
    case CloudState::Phase::FAILED:
      return std::string("{\"state\":\"failed\",\"stage\":\"") + cloud_stage_text(outcome.stage) +
             "\",\"status\":" + std::to_string(outcome.status) + ",\"code\":" + std::to_string(outcome.code) + "}";
    default:
      break;
  }
  std::string s = "{\"state\":\"idle\",\"saved\":";
  if (email_hint_or_null == nullptr) {
    s += "null";
  } else {
    s += "{\"email_hint\":\"";
    json_escape_append(s, email_hint_or_null);
    s += "\",\"region\":\"";
    json_escape_append(s, region_or_null != nullptr ? region_or_null : "");
    s += "\"}";
  }
  s += ",\"store\":\"";
  s += cloud_store_text(store);
  s += "\"}";
  return s;
}

// ---- the wiping heap (pure; host-tested) --------------------------------------------------------
namespace {
constexpr size_t WIPE_HDR = alignof(std::max_align_t) > sizeof(size_t) ? alignof(std::max_align_t) : sizeof(size_t);
}

size_t WipeHeap::capacity(const void *ptr) {
  size_t size;
  memcpy(&size, static_cast<const unsigned char *>(ptr) - WIPE_HDR, sizeof(size));
  return size;
}

void *WipeHeap::allocate(size_t size) const {
  unsigned char *b = static_cast<unsigned char *>(this->raw_alloc(WIPE_HDR + size));
  if (b == nullptr)
    return nullptr;
  memcpy(b, &size, sizeof(size));
  return b + WIPE_HDR;
}

void WipeHeap::deallocate(void *ptr) const {
  if (ptr == nullptr)
    return;
  unsigned char *b = static_cast<unsigned char *>(ptr) - WIPE_HDR;
  wipe_bytes(b, WIPE_HDR + capacity(ptr));
  this->raw_free(b);
}

void *WipeHeap::reallocate(void *ptr, size_t new_size) const {
  if (ptr == nullptr)
    return this->allocate(new_size);
  size_t old_size = capacity(ptr);
  if (new_size <= old_size)
    return ptr;  // keep the block (and its recorded size, so all of it is zeroed at release)
  void *n = this->allocate(new_size);
  if (n == nullptr)
    return nullptr;
  memcpy(n, ptr, old_size);
  this->deallocate(ptr);
  return n;
}

// ---- device: the HTTPS login ------------------------------------------------------------------
#if defined(AMBLE_CLOUD_DOOR) && !defined(AMBLE_HOST_TEST)

static constexpr size_t REPLY_MAX = 8192;
// The whole login (connect, TLS, request, headers, body) ends within this, on the monotonic clock.
static constexpr int64_t LOGIN_DEADLINE_US = 20LL * 1000 * 1000;

namespace {

struct Reply {
  char *buf;
  size_t got;
  bool truncated;  // more arrived than the cap holds; the rest was discarded
};

// The deadline watchdog. In async mode connect and TLS return to our loop, but once the request is sent
// esp_http_client_perform() reads headers and body in internal loops that keep going while bytes trickle
// in. At the deadline a one-shot timer shuts the connection's socket down, which ends any such wait at
// once. One login runs at a time (the BUSY state). Lifetime rules, all under wd_mutex:
// - The timer is created per login with that login's generation as its argument; the callback acts only
//   if that generation is still the armed one, so a late callback can never touch a newer login.
// - wd_fd is the armed login's socket, recorded on each EAGAIN return. It is cleared by the
//   HTTP_EVENT_DISCONNECTED handler, which IDF dispatches synchronously inside esp_http_client_close()
//   before it closes the transport: a callback holding the lock while it calls shutdown() therefore
//   finishes before IDF can release the descriptor, and after the event no callback can reach it.
// - The callback clears wd_fd after its shutdown; disarm clears the state, then (outside the lock)
//   stops the timer and waits for any callback already running before the timer is deleted.
std::mutex wd_mutex;
int wd_fd = -1;           // the armed login's socket; -1 when none
uint32_t wd_gen = 0;      // bumped by every arm
uint32_t wd_armed = 0;    // the generation the watchdog serves; 0 when disarmed
bool wd_fired = false;    // the armed generation's deadline passed
esp_timer_handle_t wd_timer = nullptr;

void wd_fire(void *arg) {
  const uint32_t gen = (uint32_t) (uintptr_t) arg;
  std::lock_guard<std::mutex> lock(wd_mutex);
  if (gen == 0 || gen != wd_armed)
    return;  // a callback from an earlier login: not ours to act on
  wd_fired = true;
  if (wd_fd >= 0) {
    shutdown(wd_fd, SHUT_RDWR);
    wd_fd = -1;
  }
}

void wd_forget_socket() {
  std::lock_guard<std::mutex> lock(wd_mutex);
  wd_fd = -1;
}

esp_err_t on_http_event(esp_http_client_event_t *e) {
  if (e->event_id == HTTP_EVENT_DISCONNECTED) {  // the socket is about to be closed: never shut it after this
    wd_forget_socket();
    return ESP_OK;
  }
  if (e->event_id != HTTP_EVENT_ON_DATA || e->user_data == nullptr || e->data_len <= 0)
    return ESP_OK;
  Reply *r = static_cast<Reply *>(e->user_data);
  size_t n = (size_t) e->data_len, room = REPLY_MAX - r->got;
  if (n > room) {
    n = room;
    r->truncated = true;
  }
  memcpy(r->buf + r->got, e->data, n);
  r->got += n;
  return ESP_OK;
}

bool wd_arm(int64_t delay_us) {
  uint32_t gen;
  {
    std::lock_guard<std::mutex> lock(wd_mutex);
    if (++wd_gen == 0)
      wd_gen = 1;  // 0 means disarmed
    gen = wd_gen;
    wd_fd = -1;
    wd_fired = false;
    wd_armed = gen;
  }
  esp_timer_create_args_t a = {};
  a.callback = &wd_fire;
  a.arg = (void *) (uintptr_t) gen;
  a.name = "amble_cloud_wd";
  if (esp_timer_create(&a, &wd_timer) != ESP_OK) {
    wd_timer = nullptr;
  } else if (esp_timer_start_once(wd_timer, delay_us > 1 ? delay_us : 1) == ESP_OK) {
    return true;
  } else {
    esp_timer_delete(wd_timer);
    wd_timer = nullptr;
  }
  std::lock_guard<std::mutex> lock(wd_mutex);
  wd_armed = 0;
  return false;
}

void wd_note_socket(int fd) {
  std::lock_guard<std::mutex> lock(wd_mutex);
  if (!wd_fired)
    wd_fd = fd;
}

// Called before our own close. True if this login's deadline fired.
bool wd_disarm() {
  bool fired;
  {
    std::lock_guard<std::mutex> lock(wd_mutex);
    fired = wd_fired;
    wd_fd = -1;
    wd_armed = 0;
    wd_fired = false;
  }
  if (wd_timer != nullptr) {
    // Outside the lock: a running callback may need it to finish. Once this returns ESP_OK no callback
    // runs or will run, and the timer can go. Otherwise (it should not happen from a task) the timer is
    // left alive rather than deleted under a running callback; its generation no longer matches.
    if (esp_timer_stop_blocking(wd_timer, pdMS_TO_TICKS(2000)) == ESP_OK)
      esp_timer_delete(wd_timer);
    wd_timer = nullptr;
  }
  return fired;
}

}  // namespace

CloudOutcome cloud_login_call(const CloudJob &job) {
  CloudOutcome out;
  out.stage = CloudStage::TRANSPORT;
  const int64_t deadline = esp_timer_get_time() + LOGIN_DEADLINE_US;
  auto expired = [deadline]() { return esp_timer_get_time() >= deadline; };

  // {"email":"..","password":"..","area":".."}: each field at most 128 bytes, all escaped worst case 6x.
  const size_t body_cap = CLOUD_BODY_CAP;
  char *body = static_cast<char *>(heap_caps_malloc(body_cap, MALLOC_CAP_DEFAULT));
  char *reply = static_cast<char *>(heap_caps_malloc(REPLY_MAX + 1, MALLOC_CAP_SPIRAM));
  if (reply == nullptr)
    reply = static_cast<char *>(heap_caps_malloc(REPLY_MAX + 1, MALLOC_CAP_DEFAULT));
  size_t blen = 0;
  bool built = false;
  if (body != nullptr && reply != nullptr) {
    built = cloud_build_body(job, body, body_cap, &blen);
  }

  if (built) {
    Reply rep{reply, 0, false};
    std::string url = cloud_login_url(job.region);  // region only; never anything else
    esp_http_client_config_t cfg = {};
    cfg.url = url.c_str();
    cfg.method = HTTP_METHOD_POST;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.timeout_ms = 5000;
    cfg.buffer_size = 2048;
    cfg.is_async = true;
    cfg.disable_auto_redirect = true;  // the body carries the password: never resend it elsewhere
    cfg.keep_alive_enable = false;
    cfg.event_handler = &on_http_event;
    cfg.user_data = &rep;
    cfg.user_agent = "amble-home (ESP32; +https://github.com/clwill/amble-home)";
    esp_http_client_handle_t h = esp_http_client_init(&cfg);
    if (h != nullptr) {
      esp_http_client_set_header(h, "Content-Type", "application/json");
      esp_http_client_set_header(h, "platform", "amble-home");
      esp_http_client_set_header(h, "request_source", "amble-home");
      esp_http_client_set_post_field(h, body, (int) blen);
      esp_err_t err = ESP_ERR_TIMEOUT;
      if (wd_arm(deadline - esp_timer_get_time())) {
        while ((err = esp_http_client_perform(h)) == ESP_ERR_HTTP_EAGAIN) {
          wd_note_socket(esp_http_client_get_socket(h));
          if (expired()) {
            err = ESP_ERR_TIMEOUT;
            break;
          }
          vTaskDelay(pdMS_TO_TICKS(20));
        }
        if (wd_disarm())
          err = ESP_ERR_TIMEOUT;
      }
      if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(h);
        reply[rep.got] = '\0';
        out = cloud_classify(status, reply, rep.got, rep.truncated);  // only a reply cut at the cap may end early
      }
      esp_http_client_close(h);
      esp_http_client_cleanup(h);
    }
  }
  // The password has been sent (or the attempt is over): the client held only a pointer to this buffer.
  if (body != nullptr) { wipe_bytes(body, body_cap); heap_caps_free(body); }
  if (reply != nullptr) { wipe_bytes(reply, REPLY_MAX + 1); heap_caps_free(reply); }
  return out;
}

#endif  // AMBLE_CLOUD_DOOR && !AMBLE_HOST_TEST

}  // namespace amble
}  // namespace esphome
