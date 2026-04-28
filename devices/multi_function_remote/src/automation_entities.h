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
  { "Empty Office", "EMPTY", "automation.empty_office",     "automation.trigger",           nullptr },
  { "Sofa Half",    "HALF",  "script.toggle_sofa_half_mode","script.turn_on",               nullptr },
};

static const int AUTOMATION_LIST_COUNT = sizeof(AUTOMATION_LIST) / sizeof(AUTOMATION_LIST[0]);
