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
  { "Sofa",       "light.sofa",       "sofa_light",       "sofa_brightness",       true  },
  { "Table",      "light.table",      "table_light",       nullptr,                 false },
  { "Desk",       "light.desk",       "desk_light",       "desk_brightness",       true  },
  { "Workshop",   "light.workshop",   "workshop_light",    nullptr,                 false },
  { "Bed Lights", "light.bed_lights", "bed_lights_light", "bed_lights_brightness", true  },
  { "Printers",   "light.printers",   "printers_light",    nullptr,                 false },
};

static const int LIGHTS_LIST_COUNT = sizeof(LIGHTS_LIST) / sizeof(LIGHTS_LIST[0]);
