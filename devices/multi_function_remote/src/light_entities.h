#pragma once

// ── Light entity configuration ────────────────────────────────────────────────
// Add one entry to LIGHTS_LIST for each controllable light.
//
// Field notes:
//   state_sensor_id      — ESPHome text_sensor id for light state ("on" / "off")
//   brightness_sensor_id — ESPHome sensor id for brightness (0–100 %).
//                          Set nullptr and dimmable=false for non-dimmable lights.

struct LightEntity {
  const char* name;
  const char* entity_id;
  const char* state_sensor_id;
  const char* brightness_sensor_id;
  bool        dimmable;
};

static const LightEntity LIGHTS_LIST[] = {
  { "Monitor",    "light.desk_monitor_light", "desk_monitor_light", nullptr,          false },
  { "Office",     "light.office_main",        "office_main_light",  nullptr,          false },
  { "Skadis",     "light.skadis_strip_light", "skadis_strip_light", "skadis_strip_brightness", true },
};

// Physical array length (always >= 1). Static per-entity arrays in the
// controllers are sized with this, never with the count.
static const int LIGHTS_LIST_CAPACITY = sizeof(LIGHTS_LIST) / sizeof(LIGHTS_LIST[0]);
// Number of CONFIGURED entries; may be 0. Index wrapping goes through
// wrap_index() in display_utils.h, which is defined for a count of 0.
static const int LIGHTS_LIST_COUNT = sizeof(LIGHTS_LIST) / sizeof(LIGHTS_LIST[0]);
