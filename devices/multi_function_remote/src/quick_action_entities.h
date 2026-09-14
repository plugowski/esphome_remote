#pragma once

// ── Quick-action shortcuts shown in the menu bottom panel ─────────────────
// Each icon codepoint must also appear in the menu_action_icon font glyphs in
// packages/menu.yaml, otherwise the glyph will not render.
//
// service: automation.trigger | script.turn_on | scene.turn_on |
//          input_boolean.toggle | switch.toggle | lock.lock | lock.unlock

struct QuickAction {
  const char* name;       // label — keep short (~12 chars for font_small)
  const char* icon;       // Material Symbols Rounded UTF-8 codepoint
  const char* entity_id;
  const char* service;
};

static const QuickAction QUICK_ACTION_LIST[] = {
  { "Start Work",   "\ue8f9", "scene.start_work",             "scene.turn_on"      },  // work
  { "Empty Office", "\ue7fb", "automation.empty_office",      "automation.trigger" },  // person_off
  { "Sofa Half",    "\ue02e", "script.toggle_sofa_half_mode", "script.turn_on"     },  // weekend
};
// Physical array length (always >= 1). Static per-entity arrays in the
// controllers are sized with this, never with the count.
static const int QUICK_ACTION_CAPACITY = sizeof(QUICK_ACTION_LIST) / sizeof(QUICK_ACTION_LIST[0]);
// Number of CONFIGURED entries; may be 0. Index wrapping goes through
// wrap_index() in display_utils.h, which is defined for a count of 0.
static const int QUICK_ACTION_COUNT = sizeof(QUICK_ACTION_LIST) / sizeof(QUICK_ACTION_LIST[0]);
