#pragma once

struct CoverEntity {
  const char* name;
  const char* entity_id;
  const char* state_sensor_id;
  const char* position_sensor_id;
};

static const CoverEntity COVER_LIST[] = {
  { "Living",  "cover.living_room_curtain", "living_room_curtain_state", "living_room_curtain_pos" },
  { "Bedroom", "cover.bedroom_curtain",     "bedroom_curtain_state",     "bedroom_curtain_pos" },
};

static const int COVER_LIST_COUNT = sizeof(COVER_LIST) / sizeof(COVER_LIST[0]);
