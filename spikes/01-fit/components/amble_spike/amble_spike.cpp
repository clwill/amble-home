#include "amble_spike.h"

#include "esphome/components/esp32_ble/ble.h"
#include "esphome/core/log.h"

extern "C" {
#include "esp_ble_mesh_common_api.h"
#include "esp_ble_mesh_config_model_api.h"
#include "esp_ble_mesh_defs.h"
#include "esp_ble_mesh_networking_api.h"
#include "esp_ble_mesh_provisioning_api.h"

// Internal mesh headers: no public API loads keys without provisioning.
#include "local.h"
#include "mesh/main.h"
#include "net.h"
}

namespace esphome {
namespace amble_spike {

static const char *const TAG = "amble_spike";

// Telink vendor model identity the fixtures speak (docs/origin.md item 3).
static constexpr uint16_t AMARAN_COMPANY_ID = 0x0211;
static constexpr uint16_t AMARAN_VENDOR_MODEL_ID = 0x0000;
static constexpr uint8_t AMARAN_VENDOR_OPCODE = 0x26;

static esp_ble_mesh_cfg_srv_t config_server = {
    .net_transmit = ESP_BLE_MESH_TRANSMIT(2, 20),
    .relay = ESP_BLE_MESH_RELAY_ENABLED,
    .relay_retransmit = ESP_BLE_MESH_TRANSMIT(2, 20),
    .beacon = ESP_BLE_MESH_BEACON_DISABLED,
    .gatt_proxy = ESP_BLE_MESH_GATT_PROXY_ENABLED,
    .default_ttl = 7,
};

static esp_ble_mesh_model_op_t vnd_op[] = {
    ESP_BLE_MESH_MODEL_OP(ESP_BLE_MESH_MODEL_OP_3(AMARAN_VENDOR_OPCODE, AMARAN_COMPANY_ID), 1),
    ESP_BLE_MESH_MODEL_OP(AMARAN_VENDOR_OPCODE, 1),
    ESP_BLE_MESH_MODEL_OP_END,
};

static esp_ble_mesh_model_t root_models[] = {
    ESP_BLE_MESH_MODEL_CFG_SRV(&config_server),
};

static esp_ble_mesh_model_t vnd_models[] = {
    ESP_BLE_MESH_VENDOR_MODEL(AMARAN_COMPANY_ID, AMARAN_VENDOR_MODEL_ID, vnd_op, NULL, NULL),
};

static esp_ble_mesh_elem_t elements[] = {
    ESP_BLE_MESH_ELEMENT(0, root_models, vnd_models),
};

static esp_ble_mesh_comp_t composition = {
    .cid = AMARAN_COMPANY_ID,
    .element_count = 1,
    .elements = elements,
};

static uint8_t dev_uuid[16] = {'a', 'm', 'b', 'l', 'e', '-', 's', 'p', 'i', 'k', 'e', '1', 0, 0, 0, 0};

static esp_ble_mesh_prov_t provision = {
    .uuid = dev_uuid,
};

static void prov_cb(esp_ble_mesh_prov_cb_event_t event, esp_ble_mesh_prov_cb_param_t *param) {
  ESP_LOGI(TAG, "prov event %d", (int) event);
}

static void model_cb(esp_ble_mesh_model_cb_event_t event, esp_ble_mesh_model_cb_param_t *param) {
  ESP_LOGI(TAG, "model event %d", (int) event);
}

// Never true. Exists so the linker keeps the key-injection path we will need in
// Spike 2 and so a signature drift in the pinned IDF fails the build here.
static volatile bool exercise_key_injection = false;

void AmbleSpike::setup() {
  if (this->crash_on_boot_) {
    ESP_LOGE(TAG, "BAD-OTA TEST IMAGE: aborting in setup() on purpose");
    abort();
  }
  ESP_LOGI(TAG, "spike component up; waiting for esp32_ble");
}

void AmbleSpike::loop() {
  if (this->mesh_started_ || this->mesh_init_failed_)
    return;
  if (esp32_ble::global_ble == nullptr || !esp32_ble::global_ble->is_active())
    return;

  esp_err_t err = esp_ble_mesh_register_prov_callback(prov_cb);
  if (err == ESP_OK)
    err = esp_ble_mesh_register_custom_model_callback(model_cb);
  if (err == ESP_OK)
    err = esp_ble_mesh_init(&provision, &composition);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "mesh init failed: %s", esp_err_to_name(err));
    this->mesh_init_failed_ = true;
    return;
  }
  this->mesh_started_ = true;
  ESP_LOGI(TAG, "mesh stack initialised (no bearers enabled, no keys, nothing on air)");

  if (exercise_key_injection) {
    uint8_t zero[16] = {0};
    bt_mesh_pre_provision(zero, 0, 0, 0, 0x00F0, zero);
    bt_mesh_provision();
    bt_mesh_node_local_app_key_add(0, 0, zero);
    bt_mesh_node_bind_app_key_to_model(0x00F0, AMARAN_VENDOR_MODEL_ID, AMARAN_COMPANY_ID, 0);
  }
}

void AmbleSpike::dump_config() {
  ESP_LOGCONFIG(TAG, "amble spike 1: mesh %s", this->mesh_started_ ? "initialised" : "not started");
}

}  // namespace amble_spike
}  // namespace esphome

// Linker-wrap snoop point (see __init__.py). Spike 1 only proves the wrap links
// and sits in the receive path; the side-effect-free userland decode (obfuscate +
// net decrypt per subnet key, then app decrypt) is Spike 2 work, done against a
// live fixture. The raw PDU is left untouched and passed straight through.
extern "C" void __real_bt_mesh_generic_net_recv(struct net_buf_simple *data, struct bt_mesh_net_rx *rx,
                                                enum bt_mesh_net_if net_if);
extern "C" void __wrap_bt_mesh_generic_net_recv(struct net_buf_simple *data, struct bt_mesh_net_rx *rx,
                                                enum bt_mesh_net_if net_if) {
  static uint32_t seen = 0;
  if ((++seen % 100) == 1)
    ESP_LOGD("amble_spike", "net rx #%lu via if %d len %u", (unsigned long) seen, (int) net_if, data->len);
  __real_bt_mesh_generic_net_recv(data, rx, net_if);
}

// Strong implementation of the hook from IDF patch 0001, kept so the patched
// build (the fallback if the wrap approach fails) also links.
extern "C" void amaran_mesh_access_rx(uint16_t src, uint16_t dst, const uint8_t *data, uint16_t len) {
  ESP_LOGI("amble_spike", "snoop: src 0x%04x dst 0x%04x len %u", src, dst, len);
}
