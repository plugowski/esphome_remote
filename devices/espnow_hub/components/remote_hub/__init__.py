"""remote_hub — ESP-NOW ↔ Home Assistant relay for the OLED remote.

Subscribes to every entity listed in the remote's src/*_entities.h headers,
caches their state, answers HELLO requests from remotes with a full snapshot,
broadcasts changes, and executes CMD packets as Home Assistant service calls.
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

DEPENDENCIES = ["api", "espnow"]
AUTO_LOAD = ["sensor", "text_sensor"]

CONF_NETWORK_KEY = "network_key"

remote_hub_ns = cg.esphome_ns.namespace("remote_hub")
RemoteHub = remote_hub_ns.class_("RemoteHub", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(RemoteHub),
        cv.Required(CONF_NETWORK_KEY): cv.uint32_t,
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_network_key(config[CONF_NETWORK_KEY]))
    # Needed for api::global_api_server->subscribe_home_assistant_state()
    cg.add_define("USE_API_HOMEASSISTANT_STATES")
