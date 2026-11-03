#include "identity.h"

#include <cstring>

namespace esphome {
namespace amble {

IdentityHistory::Load IdentityHistory::load(StoreBackend *backend) {
  struct {
    Header h;
    IdentityRecord r[MAX];
  } b{};
  size_t len = sizeof(b);
  this->count_ = 0;
  StoreBackend::Probe probe = backend->probe(KEY);
  if (probe == StoreBackend::Probe::ABSENT)
    return Load::EMPTY;
  if (probe != StoreBackend::Probe::PRESENT || !backend->read(KEY, &b, &len) || len < sizeof(Header) ||
      b.h.magic != MAGIC || b.h.version != VERSION || b.h.count > MAX ||
      len != sizeof(Header) + (size_t) b.h.count * sizeof(IdentityRecord))
    return Load::UNREADABLE;
  memcpy(this->records_, b.r, sizeof(IdentityRecord) * b.h.count);
  this->count_ = b.h.count;
  return Load::OK;
}

bool IdentityHistory::save(StoreBackend *backend) const {
  struct {
    Header h;
    IdentityRecord r[MAX];
  } b{};
  b.h.magic = MAGIC;
  b.h.version = VERSION;
  b.h.count = (uint16_t) this->count_;
  memcpy(b.r, this->records_, sizeof(IdentityRecord) * this->count_);
  size_t len = sizeof(Header) + this->count_ * sizeof(IdentityRecord);
  return backend->write(KEY, &b, len) && backend->commit();
}

bool IdentityHistory::remember(const uint8_t net_id[8], uint16_t address, uint32_t seq, uint32_t iv) {
  for (size_t i = 0; i < this->count_; i++) {
    IdentityRecord &r = this->records_[i];
    if (memcmp(r.net_id, net_id, 8) == 0 && r.address == address) {
      if (seq > r.seq)
        r.seq = seq;
      if (iv > r.iv)
        r.iv = iv;
      return true;
    }
  }
  if (this->count_ == MAX)
    return false;  // never evict: a forgotten identity would come back as "fresh"
  IdentityRecord &r = this->records_[this->count_++];
  memcpy(r.net_id, net_id, 8);
  r.address = address;
  r.seq = seq;
  r.iv = iv;
  return true;
}

bool IdentityHistory::resume_seq(const uint8_t net_id[8], uint16_t address, uint32_t *seq) const {
  for (size_t i = 0; i < this->count_; i++) {
    const IdentityRecord &r = this->records_[i];
    if (memcmp(r.net_id, net_id, 8) == 0 && r.address == address) {
      *seq = r.seq + SEQ_MARGIN;
      return true;
    }
  }
  return false;
}

uint32_t IdentityHistory::iv_for(const uint8_t net_id[8]) const {
  uint32_t iv = 0;
  for (size_t i = 0; i < this->count_; i++)
    if (memcmp(this->records_[i].net_id, net_id, 8) == 0 && this->records_[i].iv > iv)
      iv = this->records_[i].iv;
  return iv;
}

}  // namespace amble
}  // namespace esphome
