"""remote_hub — ESP-NOW ↔ Home Assistant relay for the OLED remote.

Subscribes to every entity listed in the remote's src/*_entities.h headers,
caches their state, answers HELLO requests from remotes with a full snapshot,
broadcasts changes, and executes CMD packets as Home Assistant service calls.

With a `history:` block it also answers HIST_REQ packets: the entity's past
values are fetched from Home Assistant's history REST API (which needs a
long-lived access token, the native API has no history call), resampled into
the requested buckets and sent back as HIST_DATA.
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import http_request, time
from esphome.const import CONF_ID, CONF_TIME_ID, CONF_URL

DEPENDENCIES = ["api", "espnow"]
AUTO_LOAD = ["sensor", "text_sensor"]

CONF_NETWORK_KEY = "network_key"
CONF_COVER_EMULATOR = "cover_emulator"
CONF_HISTORY = "history"
CONF_HTTP_REQUEST_ID = "http_request_id"
CONF_TOKEN = "token"

remote_hub_ns = cg.esphome_ns.namespace("remote_hub")
RemoteHub = remote_hub_ns.class_("RemoteHub", cg.Component)

HISTORY_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_HTTP_REQUEST_ID): cv.use_id(http_request.HttpRequestComponent),
        cv.GenerateID(CONF_TIME_ID): cv.use_id(time.RealTimeClock),
        # Home Assistant base URL, no trailing slash (http://homeassistant.local:8123)
        cv.Required(CONF_URL): cv.string_strict,
        # Long-lived access token (HA profile → Security)
        cv.Required(CONF_TOKEN): cv.string_strict,
    }
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(RemoteHub),
        cv.Required(CONF_NETWORK_KEY): cv.uint32_t,
        # Testing aid: simulate every configured cover on the hub instead of
        # talking to Home Assistant (for UI work without real covers).
        cv.Optional(CONF_COVER_EMULATOR, default=False): cv.boolean,
        cv.Optional(CONF_HISTORY): HISTORY_SCHEMA,
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_network_key(config[CONF_NETWORK_KEY]))
    cg.add(var.set_cover_emulator(config[CONF_COVER_EMULATOR]))
    # Needed for api::global_api_server->subscribe_home_assistant_state()
    cg.add_define("USE_API_HOMEASSISTANT_STATES")
    if CONF_HISTORY in config:
        hist = config[CONF_HISTORY]
        http = await cg.get_variable(hist[CONF_HTTP_REQUEST_ID])
        clock = await cg.get_variable(hist[CONF_TIME_ID])
        cg.add(var.set_history_source(http, clock, hist[CONF_URL], hist[CONF_TOKEN]))
        cg.add_define("REMOTE_HUB_HISTORY")
