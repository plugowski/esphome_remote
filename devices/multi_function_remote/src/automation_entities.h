#pragma once

// ── Automation entity configuration ──────────────────────────────────────────
// Add one entry per automation / scene / script.
//
// menu_slot  "L" or "R" → appears as a shortcut in the bottom bar
//            nullptr    → appears in the scrollable list only
//
// short_name is shown in the bottom bar (max ~5 chars to fit in the button cell).

struct AutomationEntity {
  const char* name;        // full display name for the scrollable list
  const char* short_name;  // ≤5 chars for the bottom-bar shortcut label
  const char* entity_id;   // Home Assistant entity_id
  const char* service;     // HA service to call
  const char* menu_slot;   // "L", "R", or nullptr
};

static const AutomationEntity AUTOMATION_LIST[] = {
  { "Good Morning", "MORN",  "scene.good_morning",          "scene.turn_on",                nullptr },
  { "Good Night",   "NIGHT", "scene.good_night",            "scene.turn_on",                "L"     },
  { "Away Mode",    "AWAY",  "automation.away_mode",        "automation.trigger",           nullptr },
  { "Movie Time",   "MOVIE", "scene.movie_time",            "scene.turn_on",                "R"     },
  { "Boost Heat",   "BOOST", "input_boolean.boost_heat",    "input_boolean.toggle",         nullptr },
  { "Eco Mode",     "ECO",   "input_boolean.eco_mode",      "input_boolean.toggle",         nullptr },
  { "Lock Door",    "LOCK",  "lock.front_door",             "lock.lock",                    nullptr },
  { "Arm Alarm",    "ALARM", "alarm_control_panel.home",    "alarm_control_panel.arm_away", nullptr },
};

static const int AUTOMATION_LIST_COUNT = sizeof(AUTOMATION_LIST) / sizeof(AUTOMATION_LIST[0]);
