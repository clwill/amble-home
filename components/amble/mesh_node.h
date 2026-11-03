#pragma once
// The one place amble talks to the ESP-IDF BLE-mesh stack. Proven in Spike 2:
// self-provision as our own node with imported keys (or restore from NVS),
// inject the AppKey, send Telink vendor messages from a single boundary, and
// snoop every received PDU behind a linker wrap of bt_mesh_generic_net_recv
// (no IDF patch). The Freshness gate is pure and host-tested.
#include <cstddef>
#include <cstdint>

#include "fixture.h"
#include "identity.h"

namespace esphome {
namespace amble {

struct AccessMessage {
  uint16_t src;
  uint16_t dst;
  uint32_t seq;
  const uint8_t *data;  // decrypted access payload (opcode first)
  uint16_t len;
};
// Runs on the Bluetooth (BTU) task; `msg.data` is only valid until it returns.
// Copy what you need and hand it to the main loop before touching any entity.
using AccessHandler = void (*)(const AccessMessage &msg, void *ctx);
// Also on the Bluetooth task: true if `src` is a fixture we care about. Consulted
// before a source consumes a Freshness slot, so unrelated senders cannot fill it.
using SourceFilter = bool (*)(uint16_t src, void *ctx);

// Per-source high-water mark of authenticated (iv, seq). Accepts only strictly
// newer messages; when the table is full, unknown sources are refused rather
// than evicting a tracked one (Spike 2 review, D1).
class Freshness {
 public:
  static constexpr size_t MAX_SOURCES = 32;
  bool accept(uint16_t src, uint32_t iv, uint32_t seq);
  void reset();
  size_t tracked() const { return this->count_; }

 protected:
  struct Entry {
    uint16_t src;
    uint32_t iv;
    uint32_t seq;
  };
  Entry entries_[MAX_SOURCES]{};
  size_t count_{0};
};

class MeshNode {
 public:
  // Requires esp32_ble to have the controller + Bluedroid up. Idempotent.
  bool start(const Config &cfg);
  // The ONLY transmit call site. Telink opcode 0x26, 10-byte payload, TTL 7, net/app idx 0.
  bool send_vendor(uint16_t dst, const uint8_t payload[10]);
  bool started() const { return this->started_; }
  bool failed() const { return this->failed_; }
  bool restored_from_nvs() const { return this->restored_; }
  // Where the identity history (identity.h) persists. Required before start().
  void set_store(StoreBackend *store) { this->store_ = store; }
  // Fixed text describing why start() failed (never key material). Empty if it did not.
  const char *failure_text() const { return this->failure_text_; }
  uint32_t seq() const;
  uint32_t iv_index() const;
  uint16_t own_address() const;
  void set_access_handler(AccessHandler h, void *ctx) {
    this->handler_ = h;
    this->handler_ctx_ = ctx;
  }
  void set_source_filter(SourceFilter f, void *ctx) {
    this->filter_ = f;
    this->filter_ctx_ = ctx;
  }
  bool wanted(uint16_t src) const { return this->filter_ == nullptr || this->filter_(src, this->filter_ctx_); }
  // Called from the wrap (Bluetooth task) with an authenticated, fresh payload.
  void deliver(const AccessMessage &msg) {
    if (this->handler_ != nullptr)
      this->handler_(msg, this->handler_ctx_);
  }
  Freshness &freshness() { return this->freshness_; }

 protected:
  bool self_provision_(const Config &cfg);
  bool started_{false};
  bool failed_{false};
  bool restored_{false};
  const char *failure_text_{""};
  AccessHandler handler_{nullptr};
  void *handler_ctx_{nullptr};
  SourceFilter filter_{nullptr};
  void *filter_ctx_{nullptr};
  Freshness freshness_;
  StoreBackend *store_{nullptr};
  IdentityHistory history_;
};

extern MeshNode *global_mesh_node;  // NOLINT: the wrap has no other way to reach us

}  // namespace amble
}  // namespace esphome
