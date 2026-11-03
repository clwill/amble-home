#pragma once
// Persistent configuration: two NVS slots plus an "active" pointer, so a
// commit is atomic — the new blob is fully written and verified before the
// pointer flips, and a power cut anywhere leaves the previous configuration
// loadable. ConfigStore is pure and host-tested through StoreBackend; NvsBackend
// is the device implementation.
#include <cstddef>
#include <cstdint>

#include "fixture.h"

namespace esphome {
namespace amble {

class StoreBackend {
 public:
  virtual ~StoreBackend() = default;
  // read: *len in = buffer size, out = bytes read; false if the key is absent or too large.
  virtual bool read(const char *key, void *buf, size_t *len) = 0;
  // Whether a key is present at all (whatever its size), or whether the store
  // could not even say: lets a reader tell "nothing stored" from "unreadable".
  enum class Probe : uint8_t { ABSENT, PRESENT, ERROR };
  virtual Probe probe(const char *key) = 0;
  virtual bool write(const char *key, const void *buf, size_t len) = 0;
  virtual bool erase(const char *key) = 0;
  virtual bool commit() = 0;
};

class ConfigStore {
 public:
  explicit ConfigStore(StoreBackend *backend) : backend_(backend) {}
  bool load(Config *out);
  bool commit(const Config &c);
  bool clear();
  static uint32_t crc32(const void *data, size_t len);

  static constexpr uint32_t MAGIC = 0x414D424C;  // "AMBL"
  static constexpr uint16_t VERSION = 1;
  static constexpr const char *SLOT_KEYS[2] = {"cfg0", "cfg1"};
  static constexpr const char *ACTIVE_KEY = "active";

 protected:
  struct Blob {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    Config config;
    uint32_t crc;
  };
  bool read_slot_(uint8_t slot, Config *out);
  StoreBackend *backend_;
};

#if defined(USE_ESP32) && !defined(AMBLE_HOST_TEST)
class NvsBackend : public StoreBackend {
 public:
  bool open();  // nvs_open("amble", NVS_READWRITE)
  bool read(const char *key, void *buf, size_t *len) override;
  Probe probe(const char *key) override;
  bool write(const char *key, const void *buf, size_t len) override;
  bool erase(const char *key) override;
  bool commit() override;

 protected:
  uint32_t handle_{0};
  bool open_{false};
};
#endif

}  // namespace amble
}  // namespace esphome
