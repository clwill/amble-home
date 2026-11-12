#pragma once
// AmbleComponent: the ESPHome face of amble-home. Loads the configuration from
// NVS, creates one light (plus green/magenta number and identify button) per
// fixture at boot, starts the mesh node once esp32_ble is up, routes snooped
// status replies from the Bluetooth task to the main loop, and owns the single
// guarded transmit path.
//
// Threads: the main loop owns every entity. The Bluetooth task only pushes
// decoded status events into a FreeRTOS queue; the HTTP task only hands over a
// parsed import (atomic pointer) and reads a mutex-guarded status snapshot.
#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/core/component.h"
#include "fixture.h"
#include "guard.h"
#include "light.h"
#include "mesh_node.h"
#include "cloud.h"
#include "pin.h"
#include "poll.h"
#include "store.h"
#include "telink.h"

extern "C" {
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
}

namespace esphome {
namespace amble {

enum class ImportState : uint8_t { IDLE, QUEUED, COMMITTED, FAILED };

class AmbleComponent : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  // config (codegen)
  void set_allow_group(bool allow) { this->allow_group_ = allow; }
  void set_release(bool release) { this->release_ = release; }
  void set_dev_allowlist(std::vector<uint16_t> list) { this->dev_allowlist_ = std::move(list); }
  void set_status_poll_ms(uint32_t ms) { this->status_poll_ms_ = ms; }
  void set_mdns_alias(std::string alias) { this->mdns_alias_ = std::move(alias); }

  // used by the entities (main loop)
  const Fixture &fixture(uint8_t index) const { return this->config_.fixtures[index]; }
  bool mesh_ready() const { return this->mesh_.started(); }
  bool send(uint8_t index, const uint8_t payload[telink::PAYLOAD_LEN]);
  void request_status_after(uint8_t index, uint32_t delay_ms);
  void resend_after(uint8_t index, uint32_t delay_ms);
  void publish_last_known(uint8_t index);
  void set_gm(uint8_t index, int gm);
  void identify(uint8_t index);
  void resync();  // status request to every fixture now (the Re-sync button)

  // HTTP task entry points (web.cpp)
  bool import_config(const char *json, size_t len, ParseError *err);  // validates, queues; loop() commits
  std::string status_json();  // from the mutex-guarded snapshot
  bool has_config() const { return this->has_config_; }
  // Edit-page PIN (pin.h). The gate is mutex-guarded: checked on the HTTP task, set on the main loop.
  PinGate::Result check_pin(const char *candidate);
  uint32_t pin_locked_ms();
  bool queue_pin(const char *pin);  // false if not 4–8 digits; "0000" removes the lock. loop() stores it.
  // Identify from the Edit page: the fixture's object id -> index, or -1. loop() runs it.
  int identify_index(const char *object_id) const;
  void queue_identify(uint8_t index) { this->pending_identify_.store(index); }
  // Cloud sign-in (cloud.h). HTTP task: cloud_start / cloud_result_json / cloud_forget. The worker task
  // calls finish; loop() writes and erases NVS. Behaviour exists only in a build with AMBLE_CLOUD_DOOR.
  bool cloud_start(const CloudJob &job, const char **error);  // false + *error ("a sign-in is already running", "no saved sign-in", ...)
  std::string cloud_result_json();
  bool cloud_forget();  // queues the erase of the saved sign-in for loop() and cancels any save not yet written

 protected:
  struct StatusEvent {
    uint16_t src;
    telink::Status status;
  };
  struct FixtureSnapshot {
    bool live, on;
    uint16_t brightness;
    bool hsi;
    uint16_t kelvin;
  };
  // Timer ids (numeric: the scheduler keeps name pointers, so no local strings).
  enum TimerKind : uint32_t { TIMER_STATUS_REQ = 0x100, TIMER_STATUS_WAIT = 0x200, TIMER_IDENTIFY = 0x300, TIMER_REBOOT = 0x400, TIMER_RESEND = 0x500 };
  static uint32_t timer_id(TimerKind k, uint8_t index) { return k | index; }

  static void on_access_(const AccessMessage &msg, void *ctx);
  static bool is_our_fixture_(uint16_t src, void *ctx);
  void drain_status_queue_();
  void process_pending_import_();
  void process_pending_pin_();
  void load_pin_();
  void process_pending_cloud_();
  static void cloud_worker_(void *arg);
  void cloud_run_worker_();
  void update_snapshot_();
  void request_status_(uint8_t index, bool is_retry);
  int index_for_address_(uint16_t address) const;
  void create_entities_();
  void create_diagnostics_();

  bool allow_group_{false};
  bool release_{false};
  std::vector<uint16_t> dev_allowlist_;
  TransmitGuard guard_{false, false};

  Config config_{};
  bool has_config_{false};
  // The box's mesh address seed: random, chosen on first boot, kept in NVS
  // ("seed"). 0 means it could not be read or stored, and the mesh stays down.
  uint16_t address_seed_{0};
  bool load_or_make_seed_();
  NvsBackend nvs_;
  ConfigStore store_{&nvs_};
  MeshNode mesh_;

  // entities, one set per fixture (index-aligned with config_.fixtures)
  std::vector<AmbleLight *> lights_;
  std::vector<light::LightState *> light_states_;
  std::vector<bool> light_setup_done_;
  std::vector<AmbleGmNumber *> gm_numbers_;
  std::vector<AmbleIdentifyButton *> identify_buttons_;
  std::deque<std::string> names_;  // persistent storage for entity names (StringRef)
  std::vector<uint32_t> last_seen_ms_;
  std::vector<uint8_t> pending_retry_;  // 0 none, 1 first request outstanding, 2 retry outstanding
  uint32_t status_poll_ms_{15000};
  PollScheduler poll_{15000, 150};

  // A second mDNS hostname the box answers to (release builds: the bare
  // "amble-home" next to the MAC-suffixed real name). Re-delegated on IP change.
  std::string mdns_alias_;
  uint32_t mdns_alias_ip_{0};
  uint32_t mdns_alias_retry_ms_{0};
  void serve_mdns_alias_();

  // diagnostics
  sensor::Sensor *sensor_sent_{nullptr};
  sensor::Sensor *sensor_replies_{nullptr};
  sensor::Sensor *sensor_seq_{nullptr};
  sensor::Sensor *sensor_iv_{nullptr};
  sensor::Sensor *sensor_send_fail_{nullptr};
  sensor::Sensor *sensor_timeouts_{nullptr};
  sensor::Sensor *sensor_denied_{nullptr};
  sensor::Sensor *sensor_unknown_{nullptr};
  binary_sensor::BinarySensor *mesh_up_{nullptr};
  AmbleResyncButton *resync_button_{nullptr};
  uint32_t sent_total_{0};
  uint32_t replies_total_{0};
  uint32_t denied_total_{0};
  uint32_t status_timeouts_{0};
  uint32_t send_failures_{0};
  std::atomic<uint32_t> unknown_sources_{0};  // counted on the Bluetooth task
  uint32_t last_diag_ms_{0};

  // Bluetooth task → main loop
  QueueHandle_t status_queue_{nullptr};

  // HTTP task ↔ main loop
  std::atomic<Config *> pending_import_{nullptr};
  std::atomic<ImportState> import_state_{ImportState::IDLE};
  std::atomic<const char *> import_error_{""};
  std::mutex snapshot_mutex_;
  std::string snapshot_json_;
  std::mutex pin_mutex_;
  PinGate pin_;
  char pending_pin_[PinGate::MAX_LEN + 1]{};
  std::atomic<bool> pin_pending_{false};  // a request is queued for the loop
  std::atomic<bool> pin_busy_{false};     // queued or being stored: the page waits on this
  std::atomic<const char *> pin_error_{""};
  std::atomic<int> pending_identify_{-1};
  // Cloud sign-in: cloud_mutex_ guards cloud_state_, cloud_job_, cloud_pending_save_, cloud_remember_ and
  // cloud_store_, and every change to the two pending flags (atomics only so loop() can peek without the lock).
  std::mutex cloud_mutex_;
  CloudState cloud_state_;
  CloudJob cloud_job_{};
  CloudSaved cloud_pending_save_{};
  bool cloud_remember_{false};  // the running job may still be saved; Forget clears it
  CloudStore cloud_store_{CloudStore::OK};  // the last save or erase result
  std::atomic<bool> cloud_save_pending_{false};
  std::atomic<bool> cloud_forget_pending_{false};
};

}  // namespace amble
}  // namespace esphome
