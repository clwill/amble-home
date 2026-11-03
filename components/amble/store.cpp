#include "store.h"

#include <cstring>
#include <memory>

#if defined(USE_ESP32) && !defined(AMBLE_HOST_TEST)
#include "nvs.h"
#include "nvs_flash.h"
#endif

namespace esphome {
namespace amble {

uint32_t ConfigStore::crc32(const void *data, size_t len) {
  const uint8_t *p = static_cast<const uint8_t *>(data);
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < len; i++) {
    crc ^= p[i];
    for (int b = 0; b < 8; b++)
      crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
  }
  return ~crc;
}

// Blobs and Configs are ~1.6 KB each: heap, never stack (the HTTP task that
// drives an import has a small stack).
bool ConfigStore::read_slot_(uint8_t slot, Config *out) {
  auto blob = std::make_unique<Blob>();
  size_t len = sizeof(Blob);
  if (!this->backend_->read(SLOT_KEYS[slot], blob.get(), &len) || len != sizeof(Blob))
    return false;
  if (blob->magic != MAGIC || blob->version != VERSION)
    return false;
  if (blob->crc != crc32(blob.get(), sizeof(Blob) - sizeof(blob->crc)))
    return false;
  if (blob->config.fixture_count > MAX_FIXTURES)
    return false;
  *out = blob->config;
  return true;
}

bool ConfigStore::load(Config *out) {
  uint8_t active = 0;
  size_t len = sizeof(active);
  if (!this->backend_->read(ACTIVE_KEY, &active, &len) || len != 1 || active > 1)
    return false;
  if (this->read_slot_(active, out))
    return true;
  // Active slot unreadable (corrupt or interrupted write): fall back to the other.
  return this->read_slot_(active ^ 1, out);
}

bool ConfigStore::commit(const Config &c) {
  uint8_t active = 0;
  size_t len = sizeof(active);
  bool have_active = this->backend_->read(ACTIVE_KEY, &active, &len) && len == 1 && active <= 1;
  if (have_active) {
    // If the pointed-to slot is unreadable and the other one holds the live
    // configuration (load() fell back to it), repoint at the good slot first, so
    // the slot we are about to overwrite is never the one a failed commit would
    // leave us booting from.
    auto probe = std::make_unique<Config>();
    if (!this->read_slot_(active, probe.get()) && this->read_slot_(active ^ 1, probe.get())) {
      active ^= 1;
      if (!this->backend_->write(ACTIVE_KEY, &active, sizeof(active)) || !this->backend_->commit())
        return false;
    }
  }
  uint8_t target = have_active ? (active ^ 1) : 0;

  auto blob = std::make_unique<Blob>();
  memset(blob.get(), 0, sizeof(Blob));
  blob->magic = MAGIC;
  blob->version = VERSION;
  blob->config = c;
  blob->crc = crc32(blob.get(), sizeof(Blob) - sizeof(blob->crc));
  if (!this->backend_->write(SLOT_KEYS[target], blob.get(), sizeof(Blob)) || !this->backend_->commit())
    return false;
  // Verify before flipping the pointer.
  auto check = std::make_unique<Config>();
  if (!this->read_slot_(target, check.get()) || memcmp(check.get(), &c, sizeof(Config)) != 0)
    return false;
  if (!this->backend_->write(ACTIVE_KEY, &target, sizeof(target)) || !this->backend_->commit())
    return false;
  return true;
}

bool ConfigStore::clear() {
  bool ok = true;
  ok &= this->backend_->erase(ACTIVE_KEY);
  ok &= this->backend_->erase(SLOT_KEYS[0]);
  ok &= this->backend_->erase(SLOT_KEYS[1]);
  this->backend_->commit();
  return ok;
}

#if defined(USE_ESP32) && !defined(AMBLE_HOST_TEST)
bool NvsBackend::open() {
  if (this->open_)
    return true;
  nvs_handle_t h;
  if (nvs_open("amble", NVS_READWRITE, &h) != ESP_OK)
    return false;
  this->handle_ = h;
  this->open_ = true;
  return true;
}
bool NvsBackend::read(const char *key, void *buf, size_t *len) {
  if (!this->open())
    return false;
  size_t need = 0;
  if (nvs_get_blob(this->handle_, key, nullptr, &need) != ESP_OK || need > *len)
    return false;
  *len = need;
  return nvs_get_blob(this->handle_, key, buf, len) == ESP_OK;
}
StoreBackend::Probe NvsBackend::probe(const char *key) {
  if (!this->open())
    return Probe::ERROR;
  size_t need = 0;
  esp_err_t e = nvs_get_blob(this->handle_, key, nullptr, &need);
  if (e == ESP_OK)
    return Probe::PRESENT;
  if (e == ESP_ERR_NVS_NOT_FOUND)
    return Probe::ABSENT;
  return Probe::ERROR;  // only "not found" is absence; anything else is not an answer
}
bool NvsBackend::write(const char *key, const void *buf, size_t len) {
  return this->open() && nvs_set_blob(this->handle_, key, buf, len) == ESP_OK;
}
bool NvsBackend::erase(const char *key) {
  if (!this->open())
    return false;
  esp_err_t e = nvs_erase_key(this->handle_, key);
  return e == ESP_OK || e == ESP_ERR_NVS_NOT_FOUND;
}
bool NvsBackend::commit() { return this->open() && nvs_commit(this->handle_) == ESP_OK; }
#endif

}  // namespace amble
}  // namespace esphome
