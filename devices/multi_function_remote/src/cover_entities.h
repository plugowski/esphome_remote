#pragma once

// ── Cover entity configuration ────────────────────────────────────────────────
// Add one entry to COVER_LIST for each controllable cover (blind, curtain, etc.)
//
// Field notes:
//   state_sensor_id    — ESPHome text_sensor id for cover state
//                        (open / closed / opening / closing / stopped)
//   position_sensor_id — ESPHome sensor id for cover position (0–100 %)
//                        0 = fully closed, 100 = fully open

struct CoverEntity {
  const char* name;               // Short display name (≤ 8 chars recommended)
  const char* entity_id;          // HA cover entity_id
  const char* state_sensor_id;    // ESPHome text_sensor id for cover state
  const char* position_sensor_id; // ESPHome sensor id for cover position
};

static const CoverEntity COVER_LIST[] = {
  { "Living",  "cover.living_room_curtain", "living_curtain_state",  "living_curtain_pos"  },
  { "Bedroom", "cover.bedroom_curtain",     "bedroom_curtain_state", "bedroom_curtain_pos" },
  { "Office",  "cover.office",        "office_state",    "office_pos"    },
};

// Physical array length (always >= 1). Static per-entity arrays in the
// controllers are sized with this, never with the count.
static const int COVER_LIST_CAPACITY = sizeof(COVER_LIST) / sizeof(COVER_LIST[0]);
// Number of CONFIGURED entries; may be 0. Index wrapping goes through
// wrap_index() in display_utils.h, which is defined for a count of 0.
static const int COVER_LIST_COUNT = sizeof(COVER_LIST) / sizeof(COVER_LIST[0]);
