#pragma once

// ── Controller IDs ────────────────────────────────────────────────────────────
// Stable integer constants that identify each controller.
// These are the values stored in the app_mode global and checked in YAML
// binary_sensor conditions (e.g. "return id(app_mode) == APP_AC;").
// They never need to change — only MENU_LIST below requires editing.
static const int APP_AC          = 0;
static const int APP_LIGHTS      = 1;
static const int APP_COVERS      = 2;
static const int APP_AUTOMATIONS = 3;
static const int APP_SETTINGS    = 4;
#ifdef DEMO_MODE
static const int APP_INFO        = 5;
#endif

// ── Menu entry ────────────────────────────────────────────────────────────────
struct MenuEntry {
  int         id;    // one of the APP_* constants above
  const char* icon;  // Material Symbol glyph (rendered with menu_icon_big / menu_icon_small)
  const char* name;  // uppercase label shown below the icon
};

// ── Configurable carousel ─────────────────────────────────────────────────────
// Edit this array to control which controllers appear and in what order.
// • Remove a row  → that controller is hidden from the menu entirely.
// • Reorder rows  → carousel order changes; button routing follows automatically.
// The APP_* IDs above must stay in sync with the binary_sensor conditions in
// the YAML (app_mode == APP_AC, etc.) — those never need touching.
static const MenuEntry MENU_LIST[] = {
  { APP_AC,          "\ueb3b", "AC"          },
  { APP_COVERS,      "\uec12", "COVERS"       },
  { APP_LIGHTS,      "\ue0f0", "LIGHTS"       },
  { APP_AUTOMATIONS, "\uea0b", "AUTOMATIONS"  },
  { APP_SETTINGS,    "\ue8b8", "SETTINGS"     },
#ifdef DEMO_MODE
  { APP_INFO,        "\ue88e", "INFO"         },
#endif
};
static const int MENU_LIST_COUNT = sizeof(MENU_LIST) / sizeof(MENU_LIST[0]);
