"""boot_gate — holds ESPHome's setup at one point while an early job finishes.

The remote uses it so a battery-only timer wake never reaches the display:
on_boot sees the timer wake and calls hold(), the battery is read and
reported by an on_boot automation that runs *before* the gate, and the device
goes back to deep sleep while setup is still parked here. Components ordered
after the gate (the display, the UI start) are never set up on such a wake;
on a normal wake the gate is open and setup runs straight through.

While setup waits at the gate, ESPHome keeps running the scheduler and the
loop() of every component set up before it (sensors, ESP-NOW, WiFi, API), so
asynchronous automations (delays, wait_until, script.wait) work as usual.

    boot_gate:
      id: battery_wake_gate
      setup_priority: -250      # after the job's on_boot, before the display
      timeout: 30s              # opens anyway if the job never finishes
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_TIMEOUT

boot_gate_ns = cg.esphome_ns.namespace("boot_gate")
BootGate = boot_gate_ns.class_("BootGate", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(BootGate),
        cv.Optional(CONF_TIMEOUT, default="30s"): cv.positive_time_period_milliseconds,
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_timeout_ms(config[CONF_TIMEOUT].total_milliseconds))
