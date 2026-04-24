#pragma once
#include "menu_entities.h"
#include "display_utils.h"

// ── MenuController ────────────────────────────────────────────────────────────
// Carousel mode-selector overlay — navigation logic and display rendering.
// All state lives in ESPHome globals; this class only contains pure logic.
//
// Configuration lives entirely in menu_entities.h:
//   • Add / remove / reorder entries in MENU_LIST[] to configure the carousel.
//   • The APP_* IDs in that file are stable — YAML conditions never need changing.
//
// YAML usage:
//   MenuController::open(id(menu_index), id(app_mode), id(menu_active));
//   MenuController::confirm(id(app_mode), id(menu_index), id(menu_active), id(updated_ui));
//   MenuController::prev(id(menu_index));
//   MenuController::next(id(menu_index));
//   MenuController::draw(id(oled_display), id(menu_icon_big), id(menu_icon_small),
//                        id(font_base), id(font_small), id(menu_index));

class MenuController {
public:

  // Open the carousel at the entry that matches the current app_mode.
  // Falls back to position 0 if app_mode is not found (e.g. after removing an entry).
  // No-op when only one controller is configured — nothing to switch to.
  static void open(int& menu_index, int app_mode, bool& menu_active) {
    if (MENU_LIST_COUNT <= 1) return;
    menu_index = 0;
    for (int i = 0; i < MENU_LIST_COUNT; i++) {
      if (MENU_LIST[i].id == app_mode) { menu_index = i; break; }
    }
    menu_active = true;
  }

  // Confirm selection: map carousel position → controller ID and close menu.
  static void confirm(int& app_mode, int menu_index, bool& menu_active, bool& updated_ui) {
    app_mode    = MENU_LIST[menu_index].id;
    menu_active = false;
    updated_ui  = true;
  }

  // Navigate carousel left / right (wraps around MENU_LIST_COUNT).
  static void prev(int& menu_index) {
    menu_index = (menu_index - 1 + MENU_LIST_COUNT) % MENU_LIST_COUNT;
  }

  static void next(int& menu_index) {
    menu_index = (menu_index + 1) % MENU_LIST_COUNT;
  }

  // Render carousel: small prev icon | big current icon + name | small next icon.
  // Button bar: << | OK | >>
  template<class D, class F>
  static void draw(D* it, F* icon_big, F* icon_small,
                   F* font_base, F* font_small_f, int menu_index) {
    it->clear();

    const int cur  = menu_index;
    const int prev = (cur - 1 + MENU_LIST_COUNT) % MENU_LIST_COUNT;
    const int nxt  = (cur + 1) % MENU_LIST_COUNT;

    // Side icons (small, partially clipped at screen edges)
    it->print(14,  19, icon_small, COLOR_ON, display::TextAlign::CENTER, MENU_LIST[prev].icon);
    it->print(114, 19, icon_small, COLOR_ON, display::TextAlign::CENTER, MENU_LIST[nxt].icon);

    // Centre icon (big) + mode name
    it->print(64, 17, icon_big,   COLOR_ON, display::TextAlign::CENTER, MENU_LIST[cur].icon);
    it->print(64, 43, font_base,  COLOR_ON, display::TextAlign::CENTER, MENU_LIST[cur].name);

    // Button bar
    draw_bottom_menu(it, font_small_f, "<<", "OK", ">>");

    it->display();
  }
};
