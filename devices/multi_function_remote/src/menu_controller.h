#pragma once
#include <cstring>
#include "menu_entities.h"
#include "quick_action_entities.h"
#include "display_utils.h"
#include "remote_core.h"
#include "context_strip.h"
#include "room_filter.h"

// ── MenuController ────────────────────────────────────────────────────────────
// Two-view menu overlay:
//   menu_quick_index == -1  →  full carousel (original layout)
//   menu_quick_index >= 0   →  quick-action view (big icon + name, no side icons)
//
// Navigation:
//   Left / Right  — carousel: prev / next mode | action view: prev / next action
//   Up / Down     — enters / scrolls quick-action view
//   OK            — carousel view: confirm mode | action view: trigger action
//   Stop          — carousel view: cancel menu  | action view: back to carousel
//   Round keys    — carousel view: prev / next  | action view: BACK / RUN
//
// Layout: context strip on rows 0..8 ("MENU" chip with one dot per item /
// "ACTIONS" chip; the room filter's room in the badge, if any). Carousel:
// 32 px icon centred at y=23 (rows 11..36), name in font_base centred at
// y=45 (caps 40..49), bottom bar from y=53. Quick actions: the icon on the
// left and the name beside it (see ACTION_* below). The quick-action view, scrolled with up/down or left/right, shows its position
// as vertical dots at the right edge instead (ContextStrip::drawVerticalDots)
// and BACK / RUN badge buttons (draw_soft_buttons) with no centre slot: left
// round key / stop = back to the carousel, right round key / OK = run, the
// middle key does nothing.
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

  static const int ICON_Y = 23;  // centre of the 32 px icon: rows 11..36, 2 px under the strip
  static const int NAME_Y = 45;  // centre of the font_base name: caps 40..49, 3 px over the buttons

  // Quick-action view: 32 px icon centred at (20, 31), name left-aligned at
  // x=40 and vertically centred on the icon, in the space up to the vertical
  // dots (x=120).
  static const int ACTION_ICON_X = 20;
  static const int ACTION_CY     = 31;
  static const int ACTION_NAME_X = 40;
  static const int ACTION_NAME_R = 120;

  static void open(int& menu_index, int app_mode, bool& menu_active) {
    if (MENU_LIST_COUNT <= 1) return;
    menu_index = 0;
    for (int i = 0; i < MENU_LIST_COUNT; i++) {
      if (MENU_LIST[i].id == app_mode) { menu_index = i; break; }
    }
    menu_active = true;
  }

  static void confirm(int& app_mode, int menu_index, bool& menu_active, bool& updated_ui) {
    app_mode    = MENU_LIST[wrap_index(menu_index, MENU_LIST_COUNT)].id;
    menu_active = false;
    updated_ui  = true;
  }

  static void prev(int& menu_index) {
    menu_index = wrap_index(menu_index, MENU_LIST_COUNT, -1);
  }

  static void next(int& menu_index) {
    menu_index = wrap_index(menu_index, MENU_LIST_COUNT, +1);
  }

  // -1 → last item; wraps within list
  static void scrollUp(int& quick_idx) {
    if (QUICK_ACTION_COUNT <= 0) { quick_idx = -1; return; }
    if (quick_idx < 0)
      quick_idx = QUICK_ACTION_COUNT - 1;
    else
      quick_idx = wrap_index(quick_idx, QUICK_ACTION_COUNT, -1);
  }

  // -1 → first item; wraps within list
  static void scrollDown(int& quick_idx) {
    if (QUICK_ACTION_COUNT <= 0) { quick_idx = -1; return; }
    if (quick_idx < 0)
      quick_idx = 0;
    else
      quick_idx = wrap_index(quick_idx, QUICK_ACTION_COUNT, +1);
  }

  template<class D, class F>
  static void drawTriggered(D* it, F* action_icon, F* font_base, F* font_small_f, int quick_idx) {
    it->clear();
    const QuickAction& qa = QUICK_ACTION_LIST[wrap_index(quick_idx, QUICK_ACTION_COUNT)];
    ContextStrip::draw(it, "ACTIONS", 0, 0, 0, RoomFilter::name());
    ContextStrip::drawVerticalDots(it, quick_idx, QUICK_ACTION_COUNT);
    it->print(64, ICON_Y, action_icon, COLOR_ON, display::TextAlign::CENTER, qa.icon);
    it->print(64, NAME_Y, font_base,   COLOR_ON, display::TextAlign::CENTER, qa.name);
    draw_bottom_menu(it, font_small_f, "", "", "", false);
    Toast::present(it);
  }

  template<class D, class F>
  static void draw(D* it, F* icon_big, F* icon_small, F* action_icon,
                   F* font_base, F* font_small_f, int menu_index, int quick_idx,
                   int conn_status) {
    it->clear();

    if (quick_idx < 0) {
      // ── carousel view (original full-height layout) ───────────────────────
      const int cur  = wrap_index(menu_index, MENU_LIST_COUNT);
      const int prev = wrap_index(cur, MENU_LIST_COUNT, -1);
      const int nxt  = wrap_index(cur, MENU_LIST_COUNT, +1);

      ContextStrip::draw(it, "MENU", cur, MENU_LIST_COUNT, conn_status, RoomFilter::name());
      it->print(14,  ICON_Y + 1, icon_small, COLOR_ON, display::TextAlign::CENTER, MENU_LIST[prev].icon);
      it->print(114, ICON_Y + 1, icon_small, COLOR_ON, display::TextAlign::CENTER, MENU_LIST[nxt].icon);
      it->print(64,  ICON_Y,     icon_big,   COLOR_ON, display::TextAlign::CENTER, MENU_LIST[cur].icon);
      it->print(64,  NAME_Y,     font_base,  COLOR_ON, display::TextAlign::CENTER, MENU_LIST[cur].name);
      draw_bottom_menu(it, font_small_f, "<", "OK", ">");

    } else {
      // ── quick-action view: icon on the left, name beside it ───────────────
      const QuickAction& qa = QUICK_ACTION_LIST[wrap_index(quick_idx, QUICK_ACTION_COUNT)];
      ContextStrip::draw(it, "ACTIONS", 0, 0, conn_status, RoomFilter::name());
      ContextStrip::drawVerticalDots(it, quick_idx, QUICK_ACTION_COUNT);
      it->print(ACTION_ICON_X, ACTION_CY, action_icon, COLOR_ON, display::TextAlign::CENTER, qa.icon);
      print_name_beside(it, font_base, font_small_f, qa.name, ACTION_NAME_X, ACTION_CY, ACTION_NAME_R);
      // BACK / RUN as badge buttons over the round keys, no bar or centre
      // slot: set apart from the carousel and the mode screens, whose middle
      // key is always the menu
      draw_soft_buttons(it, font_small_f, "BACK", "RUN");
    }

    Toast::present(it);
  }
};
