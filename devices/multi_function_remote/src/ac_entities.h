#pragma once

// ── AC entity configuration ───────────────────────────────────────────────────
// Add one entry to AC_LIST for each physical air-conditioner unit.
//
// Field notes:
//   modes[]      — HVAC modes your HA climate entity supports (used to cycle with MODE btn)
//   fan_modes[]  — fan speeds in cycle order (used to cycle with FAN btn)
//   *_sensor_id  — ESPHome internal sensor ID (string label in mode_ac.yaml sensor list)

struct ACEntity {
  const char*  name;               // Short display name (≤ 8 chars recommended)
  const char*  entity_id;          // HA climate entity_id
  const char*  temp_sensor_id;     // ESPHome sensor id for temperature attribute
  const char*  mode_sensor_id;     // ESPHome text_sensor id for hvac_mode state
  const char*  fan_mode_sensor_id; // ESPHome text_sensor id for fan_mode attribute
  const char** modes;              // Array of supported hvac modes
  int          modes_count;
  const char** fan_modes;          // Array of supported fan speeds (cycle order)
  int          fan_modes_count;
};

// ── per-unit mode / fan-speed arrays ─────────────────────────────────────────
static const char* AC_MODES_0[] = { "cool", "fan_only" };
static const char* AC_MODES_1[] = { "cool", "dry", "fan_only" };
static const char* AC_MODES_2[] = { "cool", "dry", "fan_only" };

static const char* AC_FAN_0[]   = { "low", "high" };               // living room
static const char* AC_FAN_1[]   = { "auto", "low", "medium", "high" };
static const char* AC_FAN_2[]   = { "auto", "low", "medium", "high" };

// ── unit list ─────────────────────────────────────────────────────────────────
static const ACEntity AC_LIST[] = {
  {
    "Living", "climate.living_room_ac",
    "living_room_ac_temp", "living_room_ac_mode", "living_room_fan_mode",
    AC_MODES_0, 2, AC_FAN_0, 2
  },
  {
    "Office", "climate.office_ac",
    "office_ac_temp", "office_ac_mode", "office_fan_mode",
    AC_MODES_1, 3, AC_FAN_1, 4
  },
  {
    "Bedroom", "climate.bedroom_ac",
    "bedroom_ac_temp", "bedroom_ac_mode", "bedroom_fan_mode",
    AC_MODES_2, 3, AC_FAN_2, 4
  },
};

static const int AC_LIST_COUNT = sizeof(AC_LIST) / sizeof(AC_LIST[0]);
