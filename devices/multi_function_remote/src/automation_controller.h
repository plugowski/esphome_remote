#pragma once
#include <string>
#include <cstring>
#include <cctype>
#include <cstdio>
#include "automation_entities.h"
#include "display_utils.h"

// ── AutomationController ──────────────────────────────────────────────────────
// Automation mode: scrollable list identical in layout to LightsController.
// Two automations can be designated menu_slot "L"/"R" — they appear as named
// shortcuts in the bottom bar and are triggered directly by the screen buttons.
//
// Button mapping:
//   MODE  (pin22)    -> triggerMenuSlot "L"  (label shown on screen left)
//   MENU  (pin5)     -> opens mode menu
//   FAN   (pin33)    -> triggerMenuSlot "R"  (label shown on screen right)
//   LEFT  (pin14)    -> nextAuto  (scroll down, same as lights)
//   RIGHT (pin26)    -> triggerSelected  (run highlighted automation)
//   T_UP  (pin23/17) -> prevAuto  (scroll up)
//   T_DN  (pin19/18) -> nextAuto  (scroll down)
//
// YAML usage:
//   AutomationController::prevAuto(id(selected_auto_index)); id(updated_ui)=true;
//   AutomationController::nextAuto(id(selected_auto_index)); id(updated_ui)=true;
//   AutomationController::triggerSelected(id(selected_auto_index),
//                                         id(auto_entity), id(auto_service));
//   AutomationController::triggerMenuSlot("L", id(auto_entity), id(auto_service));
//   AutomationController::triggerMenuSlot("R", id(auto_entity), id(auto_service));
//   AutomationController::drawConfirmation(id(oled_display),
//                                          id(font_base), id(auto_entity));
//   AutomationController::draw(id(oled_display), id(font_small),
//                               id(selected_auto_index), id(updated_ui));

class AutomationController {
public:

  // ── navigation ─────────────────────────────────────────────────────────────

  static void prevAuto(int& idx) {
    idx = (idx - 1 + AUTOMATION_LIST_COUNT) % AUTOMATION_LIST_COUNT;
  }

  static void nextAuto(int& idx) {
    idx = (idx + 1) % AUTOMATION_LIST_COUNT;
  }

  // ── trigger ────────────────────────────────────────────────────────────────

  // Loads entity + service for the currently selected list item.
  static void triggerSelected(int idx, std::string& entity, std::string& service) {
    entity = service = "";
    if (idx < 0 || idx >= AUTOMATION_LIST_COUNT) return;
    entity  = AUTOMATION_LIST[idx].entity_id;
    service = AUTOMATION_LIST[idx].service;
  }

  // Loads entity + service for a bottom-bar menu slot ("L" or "R").
  static void triggerMenuSlot(const char* slot,
                               std::string& entity, std::string& service) {
    entity = service = "";
    for (int i = 0; i < AUTOMATION_LIST_COUNT; i++) {
      if (AUTOMATION_LIST[i].menu_slot != nullptr &&
          strcmp(AUTOMATION_LIST[i].menu_slot, slot) == 0) {
        entity  = AUTOMATION_LIST[i].entity_id;
        service = AUTOMATION_LIST[i].service;
        return;
      }
    }
  }

  // ── confirmation screen ────────────────────────────────────────────────────

  // Briefly shown after triggering: displays the automation name centred.
  template<class D, class F>
  static void drawConfirmation(D* it, F* font_base, const std::string& entity_id) {
    const char* label = entity_id.c_str();
    for (int i = 0; i < AUTOMATION_LIST_COUNT; i++) {
      if (entity_id == AUTOMATION_LIST[i].entity_id) {
        label = AUTOMATION_LIST[i].name;
        break;
      }
    }
    it->clear();
    it->print(64, 32, font_base, COLOR_ON, display::TextAlign::CENTER, label);
    it->display();
  }

  // ── display ────────────────────────────────────────────────────────────────

  // Renders the automations list.  No-ops when updated_ui is false.
  //
  // Layout mirrors LightsController (4 rows × 13 px, bottom menu):
  //   name            → CENTER_LEFT at (4, y_center)
  //   pinned marker   → 4×4 px filled square at x=121 for menu-slot items
  //   bottom bar      → [L short_name] | MENU | [R short_name]
  //
  // Navigation (T_UP / T_DN / LEFT) scrolls the list.
  // RIGHT triggers the selected automation.
  // MODE / FAN trigger the L / R menu-slot shortcuts directly.
  template<class D, class F>
  static void draw(D* it, F* font_small, int selected_idx, bool& updated_ui) {
    if (!updated_ui) return;
    updated_ui = false;

    it->clear();

    // Scroll viewport — keep selected row visible
    static int scroll_top = 0;
    if (selected_idx < scroll_top)      scroll_top = selected_idx;
    if (selected_idx >= scroll_top + 4) scroll_top = selected_idx - 3;

    // Resolve bottom-bar labels from menu-slot entries
    const char* l_label = "....";
    const char* r_label = "....";
    for (int i = 0; i < AUTOMATION_LIST_COUNT; i++) {
      if (AUTOMATION_LIST[i].menu_slot == nullptr) continue;
      if (strcmp(AUTOMATION_LIST[i].menu_slot, "L") == 0) l_label = AUTOMATION_LIST[i].short_name;
      if (strcmp(AUTOMATION_LIST[i].menu_slot, "R") == 0) r_label = AUTOMATION_LIST[i].short_name;
    }

    for (int row = 0; row < 4; row++) {
      int ai = scroll_top + row;
      if (ai >= AUTOMATION_LIST_COUNT) break;

      const bool sel    = (ai == selected_idx);
      const bool pinned = (AUTOMATION_LIST[ai].menu_slot != nullptr);

      const int y_top    = row * 13;
      const int y_center = y_top + 6;

      Color fg = COLOR_ON;
      if (sel) {
        it->filled_rectangle(0, y_top, 128, 13, COLOR_ON);
        fg = COLOR_OFF;
      }

      // Name (uppercase, vertically centred)
      char buf[24];
      strncpy(buf, AUTOMATION_LIST[ai].name, sizeof(buf) - 1);
      buf[sizeof(buf) - 1] = '\0';
      for (int k = 0; buf[k]; k++) buf[k] = toupper((unsigned char)buf[k]);
      it->print(4, y_center, font_small, fg, display::TextAlign::CENTER_LEFT, buf);

      // Pinned marker: small filled square on the right edge
      if (pinned) {
        it->filled_rectangle(121, y_center - 2, 4, 4, fg);
      }
    }

    draw_bottom_menu(it, font_small, l_label, "", r_label);
    it->display();
  }
};
