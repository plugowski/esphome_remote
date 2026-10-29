"""oled_failover - `platform: ssd1306_i2c` that keeps running when the OLED is not there.

Same options as ssd1306_i2c. At boot it probes the panel on I2C; if nothing answers the remote does not mark the display
failed (which would crash the first draw) but keeps drawing into its framebuffer and sends nothing to a screen: a
virtual display, so every screen, key and radio path still works headless.

With `mirror: true` every changed frame is also streamed over USB as `oledfb` lines for the website's live screen
preview (docs: firmware page, "Live screen preview"); with a virtual display it is streamed whatever `mirror` says.
The lines go through the logger when it is on and straight to the USB-Serial-JTAG port otherwise, so no logger is
needed.

    external_components:
      - source: { type: local, path: components }
        components: [ oled_failover ]

    display:
      - platform: oled_failover
        i2c_id: oled_bus
        model: "SH1106 128x64"
        mirror: ${SCREEN_PREVIEW}
"""

import esphome.codegen as cg
from esphome.components import i2c, ssd1306_base
from esphome.components.ssd1306_base import _validate
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_LAMBDA, CONF_PAGES

AUTO_LOAD = ["ssd1306_base"]
DEPENDENCIES = ["i2c"]

CONF_MIRROR = "mirror"

oled_failover = cg.esphome_ns.namespace("oled_failover")
OledFailover = oled_failover.class_("OledFailover", ssd1306_base.SSD1306, i2c.I2CDevice)

CONFIG_SCHEMA = cv.All(
    ssd1306_base.SSD1306_SCHEMA.extend(
        {
            cv.GenerateID(): cv.declare_id(OledFailover),
            cv.Optional(CONF_MIRROR, default=False): cv.boolean,
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(i2c.i2c_device_schema(0x3C)),
    cv.has_at_most_one_key(CONF_PAGES, CONF_LAMBDA),
    _validate,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await ssd1306_base.setup_ssd1306(var, config)
    await i2c.register_i2c_device(var, config)
    cg.add(var.set_mirror(config[CONF_MIRROR]))
