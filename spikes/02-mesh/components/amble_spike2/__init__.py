"""Spike 2 component (throwaway): own-address node on the amaran mesh, userland
status snoop behind a linker wrap, guarded unicast commands to one bench
fixture. Keys come from the gitignored mesh_config.h next to this file."""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import esp32
from esphome.const import CONF_ID

DEPENDENCIES = ["esp32", "esp32_ble"]

ns = cg.esphome_ns.namespace("amble_spike2")
AmbleSpike2 = ns.class_("AmbleSpike2", cg.Component)

CONF_OWN_ADDRESS = "own_address"
CONF_TRANSMIT = "transmit"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(AmbleSpike2),
        # Our unicast address on the mesh: well clear of the fixture block, distinct
        # from the studio bridge's 0x00F0, never the app's 0x0001.
        cv.Optional(CONF_OWN_ADDRESS, default=0x00F1): cv.int_range(min=0x0002, max=0x7FFF),
        # Off = receive-only (phase A). On = unicast commands to the one target.
        cv.Optional(CONF_TRANSMIT, default=False): cv.boolean,
    }
).extend(cv.COMPONENT_SCHEMA)

MESH_SDKCONFIG = {
    "CONFIG_BT_BLE_42_FEATURES_SUPPORTED": True,
    "CONFIG_BT_GATTS_ENABLE": True,  # mesh GATT bearers need the GATT server
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
    # Standing rule 2: the stack logs NetKey/DevKey at INFO during self-provision.
    # WARNING is the IDF default; pinned here so a config change can't re-enable it.
    "CONFIG_BLE_MESH_TRACE_LEVEL_WARNING": True,
}


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_own_address(config[CONF_OWN_ADDRESS]))
    cg.add(var.set_transmit(config[CONF_TRANSMIT]))
    for key, value in MESH_SDKCONFIG.items():
        esp32.add_idf_sdkconfig_option(key, value)
    cg.add_build_flag("-Wl,--wrap=bt_mesh_generic_net_recv")
