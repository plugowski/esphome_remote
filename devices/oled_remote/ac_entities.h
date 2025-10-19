#pragma once

static const char* AC_MODES[] = {"cool", "dry", "fan_only"};
static const int AC_MODES_COUNT = sizeof(AC_MODES) / sizeof(AC_MODES[0]);

struct ACEntity {
  const char* name;
  const char* entity_id;
  const char* temp_sensor_id;
  const char* mode_sensor_id;
};

static const ACEntity AC_LIST[] = {
  { "Living", "climate.living_room_ac", "living_room_ac_temp", "living_room_ac_mode" },
  { "Office",  "climate.office_ac",      "office_ac_temp",      "office_ac_mode" },
  { "Bedroom", "climate.bedroom_ac",     "bedroom_ac_temp",     "bedroom_ac_mode" },
};

static const int AC_LIST_COUNT = sizeof(AC_LIST) / sizeof(AC_LIST[0]);