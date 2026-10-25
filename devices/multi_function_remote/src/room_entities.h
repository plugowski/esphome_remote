#pragma once

// ── Rooms ─────────────────────────────────────────────────────────────────────
// The rooms entities can be assigned to, defined once in the configurator.
// Every entity struct (ACEntity, LightEntity, CoverEntity, AutomationEntity)
// ends with an `int room`: 1-based into ROOM_LIST, 0 = no room. A row that
// leaves the field out gets 0, so entity headers written before rooms existed
// still compile and simply mean "no room".

struct RoomEntry {
  const char* name;  // shown in the context strip badge, e.g. "Bedroom"
};

static const RoomEntry ROOM_LIST[] = {
  { "Office"  },
  { "Living"  },
  { "Bedroom" },
};
// Physical array length (always >= 1). Number of CONFIGURED rooms may be 0,
// in which case the list holds one placeholder row.
static const int ROOM_LIST_CAPACITY = sizeof(ROOM_LIST) / sizeof(ROOM_LIST[0]);
static const int ROOM_LIST_COUNT    = sizeof(ROOM_LIST) / sizeof(ROOM_LIST[0]);

// Name of an entity's room, or nullptr for "no room" / an out-of-range index.
inline const char* roomName(int room) {
  return (room >= 1 && room <= ROOM_LIST_COUNT) ? ROOM_LIST[room - 1].name : nullptr;
}
