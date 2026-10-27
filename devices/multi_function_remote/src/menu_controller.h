#pragma once
#include <cctype>
#include <cstring>
#include "menu_entities.h"
#include "quick_action_entities.h"
#include "quick_action_state.h"
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
//   ●  (btn_ok)   — carousel view: confirm mode | action view: trigger action
//   ■  (btn_stop) — carousel view: cancel menu  | action view: back to carousel
//   Round keys    — carousel view: prev / next  | action view: RUN / BACK
//
// Layout: context strip on rows 0..8 ("MENU" chip with one dot per item /
// "ACTIONS" chip; the room filter's room in the badge, if any). Carousel:
// 32 px icon centred at y=23 (rows 11..36), name in font_base centred at
// y=45 (caps 40..49), bottom bar from y=53. Quick actions: the icon on the
// left and the name beside it (see ACTION_* below). The quick-action view, scrolled with up/down or left/right, shows its position
// as vertical dots at the right edge instead (ContextStrip::drawVerticalDots)
// and RUN / BACK badge buttons (draw_soft_buttons) with no centre slot: left
// round key / ● = run, right round key / ■ = back to the carousel, the
// middle key does nothing.
//
// Actions-only build (actionsOnly(): no modes in MENU_LIST, at least one quick
// action): there is no carousel, so the menu never closes and stays in the
// quick-action view - the remote is just the list of actions. The only soft
// button is RUN on the left round key (● runs too); there is no BACK, so the
// right round key, the middle key and ■ do nothing.
//
// One mode (actionsMenuOnly(): MENU_LIST_COUNT == 1, at least one quick
// action): no carousel either, so the menu key opens the quick-action view
// directly and BACK / ■ go back to the mode. With one mode and no quick
// actions the menu key has nothing to open and the screens drop its •••
// (menuKeyLive(), menu_key_shown() in display_utils.h).
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

  // Quick-action view: 26 px icon centred at (18, 31) and the name left-aligned
  // at x=36, vertically centred on it - the lights / covers / automations card
  // layout - in the space up to the vertical dots (x=120).
  static const int ACTION_ICON_X = 18;
  static const int ACTION_CY     = 31;
  static const int ACTION_NAME_X = 36;
  static const int ACTION_NAME_R = 120;

  // Toggle actions (QuickActionState): the icon sits on a 29x29 tile (x 4..32,
  // rows 17..45, centred on the icon) - filled with the icon inverted when ON,
  // an outline when OFF, a dotted outline while the state is unknown - and the
  // name moves 2 px right of it. The strip badge says ON / OFF / --.
  static const int TILE_X = ACTION_ICON_X - 14;
  static const int TILE_Y = ACTION_CY - 14;
  static const int TILE_S = 29;
  static const int TOGGLE_NAME_X = 38;

  template<class D>
  static void drawTile(D* it, uint8_t state) {
    const int x = TILE_X, y = TILE_Y, n = TILE_S;
    if (state == QuickActionState::ON) {
      it->filled_rectangle(x, y, n, n, COLOR_ON);
    } else if (state == QuickActionState::OFF) {
      it->rectangle(x, y, n, n, COLOR_ON);
    } else {
      for (int i = 1; i < n - 1; i += 3) {
        it->draw_pixel_at(x + i,     y,         COLOR_ON);
        it->draw_pixel_at(x + i + 1, y,         COLOR_ON);
        it->draw_pixel_at(x + i,     y + n - 1, COLOR_ON);
        it->draw_pixel_at(x + i + 1, y + n - 1, COLOR_ON);
        it->draw_pixel_at(x,         y + i,     COLOR_ON);
        it->draw_pixel_at(x,         y + i + 1, COLOR_ON);
        it->draw_pixel_at(x + n - 1, y + i,     COLOR_ON);
        it->draw_pixel_at(x + n - 1, y + i + 1, COLOR_ON);
      }
      return;
    }
    // corner pixels cut, like the strip chips and soft buttons
    const Color bg = COLOR_OFF;
    it->draw_pixel_at(x,         y,         bg);
    it->draw_pixel_at(x + n - 1, y,         bg);
    it->draw_pixel_at(x,         y + n - 1, bg);
    it->draw_pixel_at(x + n - 1, y + n - 1, bg);
  }

  static const char* stateBadge(uint8_t state) {
    return state == QuickActionState::ON ? "ON" : state == QuickActionState::OFF ? "OFF" : "--";
  }

  static constexpr bool actionsOnly() {
    return MENU_LIST_COUNT <= 0 && QUICK_ACTION_COUNT > 0;
  }

  // app_mode restored from flash when it is not one of the configured modes
  // (e.g. a single-mode build after a reflash): the first configured one.
  static int validMode(int app_mode) {
    for (int i = 0; i < MENU_LIST_COUNT; i++)
      if (MENU_LIST[i].id == app_mode) return app_mode;
    return MENU_LIST_COUNT > 0 ? MENU_LIST[0].id : app_mode;
  }

  // One mode and some quick actions: no carousel to step through, so the menu
  // key opens the quick actions directly and BACK returns to the mode.
  static constexpr bool actionsMenuOnly() {
    return MENU_LIST_COUNT == 1 && QUICK_ACTION_COUNT > 0;
  }

  // The middle key opens something: a carousel, or the quick actions.
  static constexpr bool menuKeyLive() {
    return MENU_LIST_COUNT > 1 || QUICK_ACTION_COUNT > 0;
  }

  // menu_open after open(): with no carousel, straight to the first action.
  static void openActions(bool& menu_active, int& quick_idx) {
    if (!actionsMenuOnly()) return;
    menu_active = true;
    quick_idx   = 0;
  }

  // Actions-only: the menu is the whole UI, pinned to the quick-action view.
  static void pinActions(bool& menu_active, int& quick_idx) {
    if (!actionsOnly()) return;
    menu_active = true;
    if (quick_idx < 0) quick_idx = 0;
  }

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
      const int qi = wrap_index(quick_idx, QUICK_ACTION_COUNT);
      const QuickAction& qa = QUICK_ACTION_LIST[qi];
      const bool toggle = QuickActionState::isToggle(qi);
      const uint8_t state = QuickActionState::get(qi);
      ContextStrip::draw(it, "ACTIONS", 0, 0, conn_status, toggle ? stateBadge(state) : RoomFilter::name());
      ContextStrip::drawVerticalDots(it, quick_idx, QUICK_ACTION_COUNT);
      if (toggle) drawTile(it, state);
      it->print(ACTION_ICON_X, ACTION_CY, action_icon,
                toggle && state == QuickActionState::ON ? COLOR_OFF : COLOR_ON,
                display::TextAlign::CENTER, qa.icon);
      // uppercase like every other card (the name is stored as typed); the
      // configurator caps it at 24 characters
      char name[25];
      strncpy(name, qa.name, sizeof(name) - 1);
      name[sizeof(name) - 1] = '\0';
      for (int k = 0; name[k]; k++) name[k] = toupper((unsigned char) name[k]);
      print_name_beside(it, font_base, font_small_f, name, toggle ? TOGGLE_NAME_X : ACTION_NAME_X,
                        ACTION_CY, ACTION_NAME_R);
      // RUN / BACK as badge buttons over the round keys, no bar or centre
      // slot: set apart from the carousel and the mode screens, whose middle
      // key is always the menu. Actions-only: nothing to go back to, so RUN
      // alone. A toggle's left key says what it will do: ON / OFF (TOGGLE
      // until the state is known).
      draw_soft_buttons(it, font_small_f, QuickActionState::runLabel(qi), actionsOnly() ? "" : "BACK");
    }

    Toast::present(it);
  }
};
