#pragma once
// The box's own HTTP surface on web_server_base (no `web_server` component, so
// ESPHome's entity controls do not exist). GET: the Home and Edit pages and
// their assets (web/box/dist, embedded gzipped at codegen) and /amble/status
// (read-only JSON). POST, PIN-gated when a PIN is set: /amble/import (the
// same-origin payload from web/importer), /amble/identify, /amble/pin. Nothing
// here controls a light beyond an identify flash. With the cloud door built in,
// POST /amble/cloud/login, GET /amble/cloud/login (the result, served once) and
// POST /amble/cloud/forget are all PIN-gated, the GET included. Forget takes the body
// {} (an empty POST is refused by the shared body check) and answers {"ok":true,"state":"queued"};
// the idle GET's "store" then reports pending -> ok | "forget failed". Handlers run on the IDF HTTP
// task (4.5 KB stack): they copy, validate and hand off; the main loop does the work.
#include <string>

#include "esphome/components/web_server_base/web_server_base.h"
#include "esphome/core/component.h"

namespace esphome {
namespace amble {

class AmbleComponent;

class AmbleWebHandler : public AsyncWebHandler, public Component {
 public:
  explicit AmbleWebHandler(AmbleComponent *parent) : parent_(parent) {}
  void setup() override;
  float get_setup_priority() const override { return setup_priority::WIFI - 1.0f; }
  bool canHandle(AsyncWebServerRequest *request) const override;
  void handleRequest(AsyncWebServerRequest *request) override;
  void handleBody(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) override;
  bool isRequestHandlerTrivial() const override { return false; }

 protected:
  static constexpr size_t MAX_BODY = 16 * 1024;
  // False (and a 401/429 already sent) when the request lacks the PIN the box requires.
  bool gate_(AsyncWebServerRequest *request);
  AmbleComponent *parent_;
  // Body accumulation is bound to one request: the declared total and what has
  // actually arrived. handleRequest consumes it only when complete.
  std::string body_;
  size_t body_total_{0};
  size_t body_received_{0};
  bool body_overflow_{false};
};

}  // namespace amble
}  // namespace esphome
