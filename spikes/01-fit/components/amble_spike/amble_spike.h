#pragma once

#include "esphome/core/component.h"

namespace esphome {
namespace amble_spike {

// Spike 1 (throwaway): brings the BLE-mesh stack up after esp32_ble has the
// controller + Bluedroid running, and references the key-injection symbols so
// the linker keeps them. Registers a node composition but never enables
// provisioning bearers and never injects keys, so it never transmits.
class AmbleSpike : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_BLUETOOTH; }
  void set_crash_on_boot(bool crash) { this->crash_on_boot_ = crash; }

 protected:
  bool crash_on_boot_{false};
  bool mesh_started_{false};
  bool mesh_init_failed_{false};
};

}  // namespace amble_spike
}  // namespace esphome
