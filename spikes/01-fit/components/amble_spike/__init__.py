"""Spike 1 component (throwaway): make the amaran BLE-mesh stack part of an
ESPHome esp-idf build and keep it linked. Never transmits. See issue #4."""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import esp32
from esphome.const import CONF_ID

DEPENDENCIES = ["esp32", "esp32_ble"]

amble_spike_ns = cg.esphome_ns.namespace("amble_spike")
AmbleSpike = amble_spike_ns.class_("AmbleSpike", cg.Component)

CONF_CRASH_ON_BOOT = "crash_on_boot"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(AmbleSpike),
        # Bad-OTA recovery test: the image aborts in setup() so it never gets
        # confirmed, and the bootloader should roll back to the previous slot.
        cv.Optional(CONF_CRASH_ON_BOOT, default=False): cv.boolean,
    }
).extend(cv.COMPONENT_SCHEMA)

# The studio bridge's mesh sdkconfig (docs/origin.md item 6), minus the target,
# flash and partition lines ESPHome owns, and minus the Bluedroid lines esp32_ble
# already sets.
MESH_SDKCONFIG = {
    "CONFIG_BT_BLE_42_FEATURES_SUPPORTED": True,
    "CONFIG_BLE_MESH": True,
    "CONFIG_BLE_MESH_NODE": True,
    "CONFIG_BLE_MESH_PB_ADV": True,
    "CONFIG_BLE_MESH_PB_GATT": True,
    "CONFIG_BLE_MESH_GATT_PROXY_SERVER": True,
    "CONFIG_BLE_MESH_RELAY": True,
    "CONFIG_BLE_MESH_SETTINGS": True,
    "CONFIG_BLE_MESH_FRIEND": False,
    "CONFIG_BLE_MESH_LOW_POWER": False,
    "CONFIG_BLE_MESH_SUBNET_COUNT": 1,
    "CONFIG_BLE_MESH_APP_KEY_COUNT": 2,
    "CONFIG_BLE_MESH_MODEL_KEY_COUNT": 2,
    "CONFIG_BLE_MESH_MODEL_GROUP_COUNT": 4,
    "CONFIG_BLE_MESH_CRPL": 20,
}

# The mesh GATT bearers (PB-GATT, GATT proxy server) need Bluedroid's GATT
# server. esp32_ble only enables it when a BLE server/client component is
# present, so we turn it on ourselves. Found by Spike 1's first link failure.
MESH_SDKCONFIG["CONFIG_BT_GATTS_ENABLE"] = True

# Internal mesh headers (local.h, mesh/main.h): under ESPHome's native esp-idf
# build the bt component already exposes core/, core/include and common/include
# to project sources, so no include-path hack is needed.


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_crash_on_boot(config[CONF_CRASH_ON_BOOT]))

    for key, value in MESH_SDKCONFIG.items():
        esp32.add_idf_sdkconfig_option(key, value)

    # Linker wrap of the mesh network receive entry point. Every bearer (ADV via
    # scan.c, GATT proxy via proxy_*.c) reaches it from a different object file
    # than net.c, so --wrap catches them all. This is the candidate replacement
    # for IDF patch 0001: snoop in our own code, no IDF fork.
    cg.add_build_flag("-Wl,--wrap=bt_mesh_generic_net_recv")
