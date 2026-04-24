#pragma once

struct ACEntity {
  const char* name;
  const char* entity_id;
  const char* temp_sensor_id;
  const char* mode_sensor_id;
  const char** modes;
  int modes_count;
};

static const char* AC_MODES_0[] = {"cool", "fan_only"};
static const char* AC_MODES_1[] = {"cool", "dry", "fan_only"};
static const char* AC_MODES_2[] = {"cool", "dry", "fan_only"};

static const ACEntity AC_LIST[] = {
  { "Living",  "climate.living_room_ac", "living_room_ac_temp", "living_room_ac_mode", AC_MODES_0, 2 },
  { "Office",  "climate.office_ac",      "office_ac_temp",      "office_ac_mode",      AC_MODES_1, 3 },
  { "Bedroom", "climate.bedroom_ac",     "bedroom_ac_temp",     "bedroom_ac_mode",     AC_MODES_2, 3 },
};

static const int AC_LIST_COUNT = sizeof(AC_LIST) / sizeof(AC_LIST[0]);
