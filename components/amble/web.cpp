#include "web.h"

#include <cstddef>
#include <cstring>

#include "amble.h"
#include "assets_gen.h"  // generated from web/box/dist by __init__.py; gitignored
#include "esphome/core/log.h"
#ifdef AMBLE_CLOUD_DOOR
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#endif

namespace esphome {
namespace amble {

static const char *const TAG = "amble.web";

struct Asset {
  const char *path;
  const char *type;
  const uint8_t *data;
  size_t size;
  bool cache;  // sql.js is versioned and large: let the browser keep it a day
};
static const Asset ASSETS[] = {
    {"/", "text/html", AMBLE_ASSET_HOME_HTML, AMBLE_ASSET_HOME_HTML_SIZE, false},
    {"/amble/", "text/html", AMBLE_ASSET_HOME_HTML, AMBLE_ASSET_HOME_HTML_SIZE, false},
    {"/amble/edit", "text/html", AMBLE_ASSET_EDIT_HTML, AMBLE_ASSET_EDIT_HTML_SIZE, false},
    {"/amble/box.js", "text/javascript", AMBLE_ASSET_BOX_JS, AMBLE_ASSET_BOX_JS_SIZE, false},
#ifdef AMBLE_HAS_CLOUD_JS
    {"/amble/cloud.js", "text/javascript", AMBLE_ASSET_CLOUD_JS, AMBLE_ASSET_CLOUD_JS_SIZE, false},
#endif
    {"/amble/importer.js", "text/javascript", AMBLE_ASSET_IMPORTER_JS, AMBLE_ASSET_IMPORTER_JS_SIZE, false},
    {"/amble/fixture-caps.json", "application/json", AMBLE_ASSET_FIXTURE_CAPS_JSON, AMBLE_ASSET_FIXTURE_CAPS_JSON_SIZE, false},
    {"/amble/sql-wasm.js", "text/javascript", AMBLE_ASSET_SQL_WASM_JS, AMBLE_ASSET_SQL_WASM_JS_SIZE, true},
    {"/amble/sql-wasm.wasm", "application/wasm", AMBLE_ASSET_SQL_WASM_WASM, AMBLE_ASSET_SQL_WASM_WASM_SIZE, true},
};

static const Asset *asset_for(AsyncWebServerRequest *request) {
  char buf[AsyncWebServerRequest::URL_BUF_SIZE];
  StringRef url = request->url_to(buf);
  for (const Asset &a : ASSETS)
    if (url == a.path)
      return &a;
  return nullptr;
}

// Minimal extraction of one string field from a small JSON object body
// ({"id":"amble_8dedb3"}, {"pin":"1234"}): no escapes needed for ids or digits.
static bool json_string_field(const std::string &body, const char *key, char *out, size_t cap) {
  std::string k = std::string("\"") + key + "\"";
  size_t p = body.find(k);
  if (p == std::string::npos)
    return false;
  p = body.find(':', p + k.size());
  if (p == std::string::npos)
    return false;
  p = body.find('"', p);
  if (p == std::string::npos)
    return false;
  p++;
  size_t e = body.find('"', p);
  if (e == std::string::npos || e - p >= cap)
    return false;
  memcpy(out, body.data() + p, e - p);
  out[e - p] = '\0';
  return true;
}

void AmbleWebHandler::setup() {
  if (web_server_base::global_web_server_base != nullptr) {
    web_server_base::global_web_server_base->init();  // starts the server; nobody else does without web_server
    web_server_base::global_web_server_base->add_handler(this);
  } else {
    ESP_LOGE(TAG, "web_server_base missing; /amble routes unavailable");
  }
}

// Zeroes through a volatile pointer so the compiler cannot drop it as a dead store.
static void wipe_bytes(char *p, size_t n) {
  volatile char *v = p;
  for (size_t i = 0; i < n; i++)
    v[i] = 0;
}
struct BodyWipe {  // zeroes the request body on every exit of the POST half
  std::string &s;
  ~BodyWipe() { wipe_bytes(s.empty() ? nullptr : &s[0], s.size()); }
};

#ifdef AMBLE_CLOUD_DOOR
// The sign-in body holds a password, and ArduinoJson copies every string it parses into blocks it later
// frees without zeroing. This allocator runs it on WipeHeap (cloud.h, host-tested), which zeroes every block
// it releases and keeps a shrunk block in place.
static void *wipe_raw_alloc(size_t size) { return heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT); }
static void wipe_raw_free(void *ptr) { heap_caps_free(ptr); }
class WipingAllocator : public ArduinoJson::Allocator {
 public:
  void *allocate(size_t size) override { return HEAP.allocate(size); }
  void deallocate(void *ptr) override { HEAP.deallocate(ptr); }
  void *reallocate(void *ptr, size_t new_size) override { return HEAP.reallocate(ptr, new_size); }

 private:
  static constexpr WipeHeap HEAP{&wipe_raw_alloc, &wipe_raw_free};
};
#endif

static bool url_is(AsyncWebServerRequest *request, const char *path) {
  char buf[AsyncWebServerRequest::URL_BUF_SIZE];
  return request->url_to(buf) == path;
}

bool AmbleWebHandler::canHandle(AsyncWebServerRequest *request) const {
  if (request->method() == HTTP_GET)
    return url_is(request, "/amble/status") || url_is(request, "/favicon.ico") || asset_for(request) != nullptr
#ifdef AMBLE_CLOUD_DOOR
           || url_is(request, "/amble/cloud/login")
#endif
        ;
  if (request->method() == HTTP_POST)
    return url_is(request, "/amble/import") || url_is(request, "/amble/identify") || url_is(request, "/amble/pin")
#ifdef AMBLE_CLOUD_DOOR
           || url_is(request, "/amble/cloud/login") || url_is(request, "/amble/cloud/forget")
#endif
        ;
  return false;
}

bool AmbleWebHandler::gate_(AsyncWebServerRequest *request) {
  auto pin = request->get_header("X-Amble-Pin");
  PinGate::Result r = this->parent_->check_pin(pin.has_value() ? pin->c_str() : nullptr);
  if (r == PinGate::Result::OK)
    return true;
  if (r == PinGate::Result::LOCKED) {
    // The IDF server knows only 200/204/400/401/404/409/422 (anything else becomes 500):
    // a lockout is a 401 whose error says "locked" and carries the wait.
    std::string m = "{\"ok\":false,\"error\":\"locked\",\"retry_ms\":" + std::to_string(this->parent_->pin_locked_ms()) + "}";
    request->send(401, "application/json", m.c_str());
    return false;
  }
  request->send(401, "application/json",
                pin.has_value() ? "{\"ok\":false,\"error\":\"pin wrong\"}" : "{\"ok\":false,\"error\":\"pin required\"}");
  return false;
}

void AmbleWebHandler::handleBody(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index,
                                 size_t total) {
  if (index == 0) {
    if (!this->body_.empty())
      wipe_bytes(&this->body_[0], this->body_.size());  // a partial body from a failed upload must not linger
    this->body_.clear();
    this->body_total_ = total;
    this->body_received_ = 0;
    this->body_overflow_ = total > MAX_BODY;
  }
  if (this->body_overflow_)
    return;
  this->body_.append(reinterpret_cast<const char *>(data), len);
  this->body_received_ = index + len;
}

void AmbleWebHandler::handleRequest(AsyncWebServerRequest *request) {
  if (request->method() == HTTP_GET) {
#ifdef AMBLE_CLOUD_DOOR
    if (url_is(request, "/amble/cloud/login")) {  // PIN-gated, unlike the other GETs: it can hand out a token once
      if (!this->gate_(request))
        return;
      std::string json = this->parent_->cloud_result_json();
      // Sent straight from this buffer (no copy), never cached: it can carry the token. Then zeroed.
      AsyncWebServerResponse *resp =
          request->beginResponse(200, "application/json", reinterpret_cast<const uint8_t *>(json.data()), json.size());
      resp->addHeader("Cache-Control", "no-store");
      request->send(resp);
      wipe_bytes(json.empty() ? nullptr : &json[0], json.size());
      return;
    }
#endif
    if (url_is(request, "/amble/status")) {
      std::string json = this->parent_->status_json();
      request->send(200, "application/json", json.c_str());
      return;
    }
    if (url_is(request, "/favicon.ico")) {  // browsers ask on every page; an empty answer beats a dropped connection
      AsyncWebServerResponse *resp = request->beginResponse(204, "image/x-icon", "");
      resp->addHeader("Cache-Control", "max-age=86400");
      request->send(resp);
      return;
    }
    const Asset *a = asset_for(request);
    if (a == nullptr) {
      request->send(404);
      return;
    }
    AsyncWebServerResponse *resp = request->beginResponse(200, a->type, a->data, a->size);
    resp->addHeader("Content-Encoding", "gzip");
    resp->addHeader("Cache-Control", a->cache ? "max-age=86400" : "no-cache");
    request->send(resp);
    return;
  }
  // POST. Take the accumulated body and reset, whatever happens next.
  std::string body = std::move(this->body_);
  BodyWipe body_wipe{body};
  size_t total = this->body_total_, received = this->body_received_;
  bool overflow = this->body_overflow_;
  this->body_.clear();
  this->body_total_ = this->body_received_ = 0;
  this->body_overflow_ = false;

  if (overflow) {
    request->send(422, "application/json", "{\"ok\":false,\"error\":\"payload larger than 16 KB\"}");  // no 413 in the IDF server
    return;
  }
  // The body must belong to THIS request and be complete: a declared length that
  // was fully received. An empty POST, or leftovers from an interrupted upload,
  // are rejected rather than imported.
  if (total == 0 || received != total || body.size() != total || request->contentLength() != total) {
    request->send(400, "application/json", "{\"ok\":false,\"error\":\"request body missing or incomplete\"}");
    return;
  }
  if (!this->gate_(request))
    return;
  if (url_is(request, "/amble/identify")) {
    char id[32];
    int idx = json_string_field(body, "id", id, sizeof(id)) ? this->parent_->identify_index(id) : -1;
    if (idx < 0) {
      request->send(404, "application/json", "{\"ok\":false,\"error\":\"unknown fixture\"}");
    } else if (!this->parent_->mesh_ready()) {
      request->send(409, "application/json", "{\"ok\":false,\"error\":\"mesh not ready\"}");  // no 503 in the IDF server
    } else {
      this->parent_->queue_identify((uint8_t) idx);
      request->send(200, "application/json", "{\"ok\":true}");
    }
    return;
  }
  if (url_is(request, "/amble/pin")) {
    char pin[PinGate::MAX_LEN + 2];
    if (json_string_field(body, "pin", pin, sizeof(pin)) && this->parent_->queue_pin(pin))
      request->send(200, "application/json", "{\"ok\":true,\"state\":\"queued\"}");  // GET /amble/status: pin_pending -> pin_set / pin_error
    else
      request->send(400, "application/json", "{\"ok\":false,\"error\":\"pin must be 4-8 digits\"}");
    return;
  }
#ifdef AMBLE_CLOUD_DOOR
  if (url_is(request, "/amble/cloud/forget")) {
    this->parent_->cloud_forget();  // queued; GET /amble/cloud/login "store": pending -> ok | "forget failed"
    request->send(200, "application/json", "{\"ok\":true,\"state\":\"queued\"}");
    return;
  }
  if (url_is(request, "/amble/cloud/login")) {
    static const char *const BAD = "{\"ok\":false,\"error\":\"bad sign-in request\"}";
    CloudJob job{};
    bool valid = false;
    {
      // A request-local document on the wiping allocator: it goes out of scope at the end of this block on
      // every path (parsed, refused, not JSON), and every block it used, fragments included, is zeroed then.
      WipingAllocator alloc;
      JsonDocument doc(&alloc);
      if (!deserializeJson(doc, body.data(), body.size()) && doc.is<JsonObject>()) {
        JsonObject root = doc.as<JsonObject>();
        JsonVariant saved = root["saved"];
        if (!saved.isNull()) {
          if (saved.is<bool>() && saved.as<bool>()) {
            job.use_saved = true;
            valid = true;
          }
        }
        if (!valid && (saved.isNull() || saved.is<bool>())) {
          JsonVariant e = root["email"], p = root["password"], r = root["region"], rem = root["remember"];
          if (e.is<const char *>() && p.is<const char *>() && r.is<const char *>() && (rem.isNull() || rem.is<bool>())) {
            const char *es = e.as<const char *>(), *ps = p.as<const char *>(), *rs = r.as<const char *>();
            size_t el = strlen(es), pl = strlen(ps);
            if (el >= 1 && el <= CLOUD_EMAIL_MAX && pl >= 1 && pl <= CLOUD_PW_MAX && cloud_region_valid(rs)) {
              memcpy(job.email, es, el);
              memcpy(job.password, ps, pl);
              job.region[0] = rs[0];
              job.region[1] = rs[1];
              job.remember = rem.isNull() ? false : rem.as<bool>();
              valid = true;
            }
          }
        }
      }
    }
    if (!valid) {
      job.wipe();
      request->send(400, "application/json", BAD);
      return;
    }
    job.email[CLOUD_EMAIL_MAX] = '\0';
    job.password[CLOUD_PW_MAX] = '\0';
    job.region[2] = '\0';
    const char *error = nullptr;
    bool started = this->parent_->cloud_start(job, &error);
    job.wipe();
    if (started) {
      request->send(200, "application/json", "{\"ok\":true,\"state\":\"queued\"}");
    } else {
      std::string why = error != nullptr ? error : "sign-in failed to start";
      std::string msg = "{\"ok\":false,\"error\":\"" + why + "\"}";
      request->send(why == CLOUD_BUSY ? 409 : 400, "application/json", msg.c_str());
    }
    return;
  }
#endif
  // POST /amble/import
  ParseError err;
  if (this->parent_->import_config(body.c_str(), body.size(), &err)) {
    // Queued; the main loop commits to NVS and reboots. Poll GET /amble/status
    // "import.state": queued -> committed (then the box reboots) | failed (+error).
    request->send(200, "application/json", "{\"ok\":true,\"state\":\"queued\"}");  // the IDF server has no 202
  } else if (err == ParseError::OK) {
    request->send(409, "application/json", "{\"ok\":false,\"error\":\"an import is already in progress\"}");
  } else {
    std::string msg = std::string("{\"ok\":false,\"error\":\"") + parse_error_text(err) + "\"}";
    request->send(400, "application/json", msg.c_str());
  }
}

}  // namespace amble
}  // namespace esphome
