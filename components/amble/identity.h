#pragma once
// Safety state for mesh identities this box has used. A mesh
// node's replay protection lives in the fixtures, keyed by (network, source
// address, sequence number): if the box ever returns to an identity it used
// before with a restarted sequence number, fixtures drop its commands as
// replays and encryption nonces repeat. So whenever the box leaves an identity
// it records where that identity's sequence number and IV index got to, and
// when it comes back it resumes from there. Records are never evicted and an
// unreadable record is never mistaken for "never used": both fail closed at
// the caller. Pure and host-tested; the device persists the blob through
// StoreBackend (NVS key "ident").
#include <cstddef>
#include <cstdint>

#include "store.h"

namespace esphome {
namespace amble {

struct IdentityRecord {
  uint8_t net_id[8];  // the network's public identifier (k3 of the NetKey; beaconed in clear), never the key
  uint16_t address;
  uint32_t seq;
  uint32_t iv;
};

class IdentityHistory {
 public:
  static constexpr size_t MAX = 16;  // distinct (network, address) identities a box may ever use
  static constexpr uint32_t MAGIC = 0x4944454E;  // "IDEN"
  static constexpr uint16_t VERSION = 1;
  static constexpr uint32_t SEQ_MARGIN = 32;     // resumed sequence numbers skip ahead by this much
  static constexpr const char *KEY = "ident";

  // UNREADABLE: the store could not say, or something is stored that cannot be trusted
  enum class Load : uint8_t { EMPTY, OK, UNREADABLE };
  Load load(StoreBackend *backend);
  bool save(StoreBackend *backend) const;  // false: the write or commit failed
  // Record (or raise) the state reached under an identity. Never lowers a stored
  // value. False when the identity is new and the history is full: nothing changes.
  bool remember(const uint8_t net_id[8], uint16_t address, uint32_t seq, uint32_t iv);
  // The sequence number to resume with under this identity, or false if never used.
  bool resume_seq(const uint8_t net_id[8], uint16_t address, uint32_t *seq) const;
  // The highest IV index seen on this network under any address (0 if never seen).
  uint32_t iv_for(const uint8_t net_id[8]) const;
  size_t size() const { return this->count_; }

 protected:
  // Stored as a header plus exactly `count` records, so the capacity (MAX) can
  // change in a later firmware without an older record reading as corrupt.
  struct Header {
    uint32_t magic;
    uint16_t version;
    uint16_t count;
  };
  IdentityRecord records_[MAX]{};
  size_t count_{0};
};

}  // namespace amble
}  // namespace esphome
