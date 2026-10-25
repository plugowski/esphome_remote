#pragma once
#include <string>
#include <cstring>
#include <cctype>
#include <cstdio>
#include "automation_entities.h"
#include "display_utils.h"
#include "remote_core.h"
#include "room_entities.h"
#include "room_filter.h"

// ── AutomationController ──────────────────────────────────────────────────────
// Automations mode: scenes, automations, scripts and toggles to run. Two views,
// chosen at build time by the AUTOMATIONS_CARD_VIEW substitution (global
// automations_card_view):
//
//   list view (card_view = false) — 4 rows like the lights list: the name, and
//       the kind (AUTOMATION / SCRIPT / SCENE / ...) as a small tag on the right
//   card view (card_view = true)  — one per screen like a quick action: the
//       automation's 26 px icon on the left (lights bulb size), the name centred right of it
//
// Both: context strip (AUTOMATIONS chip, page dots - one per card / per 4 rows -
// and the room filter's room or ALL in the badge). Entries outside the room
// filter are skipped (no-room entries show everywhere); a room with none shows
// the empty-room screen.
//
// Shortcut keys: AUTOMATION_SHORTCUT_L / _R run a fixed automation from any
// position, drawn as soft buttons over the round keys - icon only, or icon
// and a short label. A key without a shortcut has no button.
//
// Button mapping (routing in buttons.yaml / the layouts):
//   round keys L / R      → shortcut L / R
//   ← / → and ↑ / ↓       → previous / next automation
//   OK                    → run the selected automation
//   MENU                  → mode menu
// Every run shows the SENT toast.
//
// YAML usage:
//   AutomationController::prevAuto(id(selected_auto_index)); id(updated_ui)=true;
//   AutomationController::nextAuto(id(selected_auto_index)); id(updated_ui)=true;
//   AutomationController::triggerSelected(id(selected_auto_index),
//                                         id(auto_entity), id(auto_service));
//   AutomationController::triggerShortcut(AUTOMATION_SHORTCUT_L,
//                                         id(auto_entity), id(auto_service));
//   AutomationController::draw(id(oled_display), id(font_base), id(font_small),
//                              id(automation_icon), id(shortcut_icon),
//                              id(selected_auto_index), id(automations_card_view),
//                              id(updated_ui), id(conn_status));

class AutomationController {
public:

  static const int ROWS   = 4;
  static const int ROW_H  = 10;
  static const int LIST_Y = ContextStrip::CONTENT_Y + 1;  // rows 11..50
  static const int CARD_CY = 31;                          // card: icon / name centre row

  // ── navigation (skips entries outside the room filter) ─────────────────────

  static void prevAuto(int& idx) {
    idx = RoomFilter::step(idx, AUTOMATION_LIST_COUNT, -1, automationRoom);
  }

  static void nextAuto(int& idx) {
    idx = RoomFilter::step(idx, AUTOMATION_LIST_COUNT, +1, automationRoom);
  }

  // ── trigger ────────────────────────────────────────────────────────────────

  // Loads entity + service for the selected automation (empty when there is
  // nothing to run, e.g. an empty room).
  static void triggerSelected(int idx, std::string& entity, std::string& service) {
    entity = service = "";
    if (idx < 0 || idx >= AUTOMATION_LIST_COUNT) return;
    if (!RoomFilter::visible(idx, AUTOMATION_LIST_COUNT, automationRoom)) return;
    entity  = AUTOMATION_LIST[idx].entity_id;
    service = AUTOMATION_LIST[idx].service;
  }

  // Loads entity + service for a shortcut key (empty when the key has none).
  static void triggerShortcut(const AutomationShortcut& sc, std::string& entity, std::string& service) {
    entity = service = "";
    if (sc.index < 0 || sc.index >= AUTOMATION_LIST_COUNT) return;
    entity  = AUTOMATION_LIST[sc.index].entity_id;
    service = AUTOMATION_LIST[sc.index].service;
  }

  // Kind of entry from its entity_id domain, for the list tag / card subtitle.
  static const char* kind(int idx) {
    const char* e = AUTOMATION_LIST[idx].entity_id;
    if (strncmp(e, "automation.", 11) == 0)    return "AUTOMATION";
    if (strncmp(e, "script.", 7) == 0)         return "SCRIPT";
    if (strncmp(e, "scene.", 6) == 0)          return "SCENE";
    if (strncmp(e, "switch.", 7) == 0)         return "SWITCH";
    if (strncmp(e, "input_boolean.", 14) == 0) return "TOGGLE";
    if (strncmp(e, "lock.", 5) == 0)           return "LOCK";
    if (strncmp(e, "alarm_control_panel.", 20) == 0) return "ALARM";
    return "ACTION";
  }

  // ── display ────────────────────────────────────────────────────────────────

  template<class D, class F>
  static void draw(D* it, F* font_base, F* font_small, F* action_icon, F* shortcut_icon,
                   int selected_idx, bool card_view, bool& updated_ui, int conn_status) {
    if (!updated_ui) return;
    updated_ui = false;

    // Nothing in the chosen room: empty state (the run keys are gated off too;
    // the shortcut keys keep working, so their buttons stay)
    if (AUTOMATION_LIST_COUNT > 0 && !RoomFilter::any(AUTOMATION_LIST_COUNT, automationRoom)) {
      drawEmptyRoom(it, font_base, font_small, "AUTOMATIONS", "automations", conn_status, false);
      drawShortcutKeys(it, font_small, shortcut_icon);
      Toast::present(it);
      return;
    }

    it->clear();
    const char* badge = RoomFilter::active() ? RoomFilter::name() : "ALL";

    if (card_view) {
      const int ai = wrap_index(selected_idx, AUTOMATION_LIST_CAPACITY);
      ContextStrip::draw(it, "AUTOMATIONS",
                         RoomFilter::position(ai, AUTOMATION_LIST_COUNT, automationRoom),
                         RoomFilter::visibleCount(AUTOMATION_LIST_COUNT, automationRoom), conn_status,
                         badge);
      // icon on the left, vertically centred; the name centred (one line or
      // two) in the space right of it
      it->print(18, CARD_CY, action_icon, COLOR_ON, display::TextAlign::CENTER, AUTOMATION_LIST[ai].icon);
      char name[24];
      upper(name, sizeof(name), AUTOMATION_LIST[ai].name);
      print_name_beside(it, font_base, font_small, name, 36, CARD_CY, 124, true);
    } else {
      // page of 4 (among the entries in the room filter) holding the selection
      const int shown      = RoomFilter::visibleCount(AUTOMATION_LIST_COUNT, automationRoom);
      const int page       = RoomFilter::position(selected_idx, AUTOMATION_LIST_COUNT, automationRoom) / ROWS;
      const int page_count = (shown + ROWS - 1) / ROWS;
      ContextStrip::draw(it, "AUTOMATIONS", page, page_count, conn_status, badge);
      for (int row = 0; row < ROWS; row++) {
        const int ai = RoomFilter::at(page * ROWS + row, AUTOMATION_LIST_COUNT, automationRoom);
        if (ai < 0) break;
        const int y_top    = LIST_Y + row * ROW_H;
        const int y_center = y_top + 4;
        Color fg = COLOR_ON;
        if (ai == selected_idx) {
          it->filled_rectangle(0, y_top, 128, ROW_H, COLOR_ON);
          fg = COLOR_OFF;
        }
        // kind tag on the right; the name is trimmed to stop 4 px before it
        const char* k  = kind(ai);
        const int   kx = 124 - ContextStrip::textWidth(k);
        ContextStrip::text(it, kx, y_top + 3, k, fg);
        char name[24];
        upper(name, sizeof(name), AUTOMATION_LIST[ai].name);
        int tx, ty, tw, th;
        it->get_text_bounds(4, y_center, name, font_small, display::TextAlign::CENTER_LEFT, &tx, &ty, &tw, &th);
        while (4 + tw > kx - 4 && strlen(name) > 1) {
          name[strlen(name) - 1] = '\0';
          it->get_text_bounds(4, y_center, name, font_small, display::TextAlign::CENTER_LEFT, &tx, &ty, &tw, &th);
        }
        it->print(4, y_center, font_small, fg, display::TextAlign::CENTER_LEFT, name);
      }
    }

    draw_bottom_menu(it, font_small, "", nullptr, "");   // centre ••• only
    drawShortcutKeys(it, font_small, shortcut_icon);
    Toast::present(it);
  }

private:

  static void upper(char* dst, size_t n, const char* src) {
    strncpy(dst, src, n - 1);
    dst[n - 1] = '\0';
    for (int k = 0; dst[k]; k++) dst[k] = toupper((unsigned char) dst[k]);
  }

  // The two shortcut soft buttons (draw_bottom_menu's side geometry): icon
  // only, or icon + label (font_small when it fits, else the 3x5 font).
  template<class D, class F>
  static void drawShortcutKeys(D* it, F* font_small, F* shortcut_icon) {
    drawShortcutKey(it, font_small, shortcut_icon, AUTOMATION_SHORTCUT_L, 2);
    drawShortcutKey(it, font_small, shortcut_icon, AUTOMATION_SHORTCUT_R, 86);
  }

  template<class D, class F>
  static void drawShortcutKey(D* it, F* font_small, F* shortcut_icon,
                              const AutomationShortcut& sc, int x) {
    if (sc.index < 0 || sc.index >= AUTOMATION_LIST_COUNT) return;
    const int w = 40, cx = x + w / 2, cy = SOFT_BTN_Y + 5;
    draw_soft_button(it, x, w);
    const bool has_icon  = sc.icon && sc.icon[0];
    const bool has_label = sc.label && sc.label[0];
    int iw = 0, ix, iy, ih;
    if (has_icon) it->get_text_bounds(0, 0, sc.icon, shortcut_icon, display::TextAlign::TOP_LEFT, &ix, &iy, &iw, &ih);
    if (!has_label) {
      if (has_icon) it->print(cx, cy, shortcut_icon, COLOR_OFF, display::TextAlign::CENTER, sc.icon);
      return;
    }
    // icon + label as one centred group
    int lw, lx, ly, lh;
    it->get_text_bounds(0, 0, sc.label, font_small, display::TextAlign::TOP_LEFT, &lx, &ly, &lw, &lh);
    const int gap = has_icon ? 3 : 0;
    if (iw + gap + lw <= w - 4) {
      const int gx = cx - (iw + gap + lw) / 2;
      if (has_icon) it->print(gx, cy, shortcut_icon, COLOR_OFF, display::TextAlign::CENTER_LEFT, sc.icon);
      it->print(gx + iw + gap, cy, font_small, COLOR_OFF, display::TextAlign::CENTER_LEFT, sc.label);
    } else {
      const int tw = ContextStrip::textWidth(sc.label);
      const int gx = cx - (iw + gap + tw) / 2;
      if (has_icon) it->print(gx, cy, shortcut_icon, COLOR_OFF, display::TextAlign::CENTER_LEFT, sc.icon);
      ContextStrip::text(it, gx + iw + gap, cy - 2, sc.label, COLOR_OFF);
    }
  }
};
