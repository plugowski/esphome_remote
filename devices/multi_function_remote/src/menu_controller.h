#pragma once
#include "menu_entities.h"
#include "quick_action_entities.h"
#include "display_utils.h"

// ── MenuController ────────────────────────────────────────────────────────────
// Two-view menu overlay:
//   menu_quick_index == -1  →  full carousel (original layout)
//   menu_quick_index >= 0   →  quick-action view (big icon + name, no side icons)
//
// Navigation:
//   Left / Right  — carousel; resets quick_index to -1 (returns to carousel view)
//   Up / Down     — enters / scrolls quick-action view
//   OK            — carousel view: confirm mode | action view: trigger action
//   Stop          — carousel view: cancel menu  | action view: back to carousel
//
// YAML usage:
//   MenuController::open(id(menu_index), id(app_mode), id(menu_active));
//   MenuController::confirm(id(app_mode), id(menu_index), id(menu_active), id(updated_ui));
//   MenuController::prev(id(menu_index));
//   MenuController::next(id(menu_index));
//   MenuController::scrollUp(id(menu_quick_index));
//   MenuController::scrollDown(id(menu_quick_index));
//   MenuController::draw(id(oled_display), id(menu_icon_big), id(menu_icon_small),
//                        id(menu_action_icon), id(font_base), id(font_small),
//                        id(menu_index), id(menu_quick_index));

class MenuController {
public:

  static void open(int& menu_index, int app_mode, bool& menu_active) {
    if (MENU_LIST_COUNT <= 1) return;
    menu_index = 0;
    for (int i = 0; i < MENU_LIST_COUNT; i++) {
      if (MENU_LIST[i].id == app_mode) { menu_index = i; break; }
    }
    menu_active = true;
  }

  static void confirm(int& app_mode, int menu_index, bool& menu_active, bool& updated_ui) {
    app_mode    = MENU_LIST[menu_index].id;
    menu_active = false;
    updated_ui  = true;
  }

  static void prev(int& menu_index) {
    menu_index = (menu_index - 1 + MENU_LIST_COUNT) % MENU_LIST_COUNT;
  }

  static void next(int& menu_index) {
    menu_index = (menu_index + 1) % MENU_LIST_COUNT;
  }

  // -1 → last item; wraps within list
  static void scrollUp(int& quick_idx) {
    if (quick_idx < 0)
      quick_idx = QUICK_ACTION_COUNT - 1;
    else
      quick_idx = (quick_idx - 1 + QUICK_ACTION_COUNT) % QUICK_ACTION_COUNT;
  }

  // -1 → first item; wraps within list
  static void scrollDown(int& quick_idx) {
    if (quick_idx < 0)
      quick_idx = 0;
    else
      quick_idx = (quick_idx + 1) % QUICK_ACTION_COUNT;
  }

  template<class D, class F>
  static void drawTriggered(D* it, F* action_icon, F* font_base, F* font_small_f, int quick_idx) {
    it->clear();
    const QuickAction& qa = QUICK_ACTION_LIST[quick_idx];
    it->print(64, 17, action_icon, COLOR_ON, display::TextAlign::CENTER, qa.icon);
    it->print(64, 43, font_base,   COLOR_ON, display::TextAlign::CENTER, qa.name);
    draw_bottom_menu(it, font_small_f, "", "", "");
    it->display();
  }

  template<class D, class F>
  static void draw(D* it, F* icon_big, F* icon_small, F* action_icon,
                   F* font_base, F* font_small_f, int menu_index, int quick_idx) {
    it->clear();

    if (quick_idx < 0) {
      // ── carousel view (original full-height layout) ───────────────────────
      const int cur  = menu_index;
      const int prev = (cur - 1 + MENU_LIST_COUNT) % MENU_LIST_COUNT;
      const int nxt  = (cur + 1) % MENU_LIST_COUNT;

      it->print(14,  19, icon_small, COLOR_ON, display::TextAlign::CENTER, MENU_LIST[prev].icon);
      it->print(114, 19, icon_small, COLOR_ON, display::TextAlign::CENTER, MENU_LIST[nxt].icon);
      it->print(64,  17, icon_big,   COLOR_ON, display::TextAlign::CENTER, MENU_LIST[cur].icon);
      it->print(64,  43, font_base,  COLOR_ON, display::TextAlign::CENTER, MENU_LIST[cur].name);
      draw_bottom_menu(it, font_small_f, "<", "OK", ">");

    } else {
      // ── quick-action view (big icon + name, no side icons) ────────────────
      const QuickAction& qa = QUICK_ACTION_LIST[quick_idx];
      it->print(64, 17, action_icon, COLOR_ON, display::TextAlign::CENTER, qa.icon);
      it->print(64, 43, font_base,   COLOR_ON, display::TextAlign::CENTER, qa.name);
      draw_bottom_menu(it, font_small_f, "", "", "");
    }

    it->display();
  }
};
