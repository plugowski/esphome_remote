#pragma once
#include <string>
#include <cstring>
#include <cctype>
#include <cmath>
#include <cstdio>
#include "cover_entities.h"
#include "display_utils.h"

// ── CoverController ───────────────────────────────────────────────────────────
// Cover mode: entity selection, open/close/stop/position actions, display.
//
// State and position for every cover are stored in private static arrays so
// that switching covers immediately shows the last received values instead of
// reverting to Unknown while waiting for HA to re-push data.
//
// YAML usage:
//   CoverController::prevCover(id(selected_cover_index));
//   CoverController::nextCover(id(selected_cover_index));
//   CoverController::selectCover(id(selected_cover_index),
//                                id(selected_cover), id(selected_cover_name));
//   CoverController::syncCoverState(sensor_idx, x, id(updated_ui));
//   CoverController::syncCoverPosition(sensor_idx, x, id(updated_ui));
//   CoverController::positionUp(id(selected_cover_index), id(updated_ui));
//   CoverController::positionDown(id(selected_cover_index), id(updated_ui));
//   CoverController::getPosition(id(selected_cover_index))   → int
//   CoverController::isClosed(id(selected_cover_index))      → bool
//   CoverController::draw(id(oled_display),
//                         id(ac_symbols), id(menu_icon_small),
//                         id(font_big), id(font_base), id(font_small),
//                         id(selected_cover_index), id(selected_cover_name),
//                         id(updated_ui));

class CoverController {
public:

  // ── navigation ─────────────────────────────────────────────────────────────

  static void prevCover(int& idx) {
    idx = (idx - 1 + COVER_LIST_COUNT) % COVER_LIST_COUNT;
  }

  static void nextCover(int& idx) {
    idx = (idx + 1) % COVER_LIST_COUNT;
  }

  // ── HA state sync — always stores for every sensor index ──────────────────

  static void syncCoverState(int sensor_idx, const std::string& ha_state,
                              bool& updated_ui) {
    if (ha_state.empty()) return;
    std::string& s = st(sensor_idx);
    if (s == ha_state) return;
    s = ha_state;
    if (ha_state == "unavailable" || ha_state == "unknown") pos(sensor_idx) = -1;
    updated_ui = true;
  }

  static void syncCoverPosition(int sensor_idx, float ha_pos_f,
                                 bool& updated_ui) {
    int ha_pos = (std::isnan(ha_pos_f) || ha_pos_f < 0) ? -1
                                                         : static_cast<int>(ha_pos_f);
    int& p = pos(sensor_idx);
    if (p == ha_pos) return;
    p = ha_pos;
    updated_ui = true;
  }

  // ── position control (10 % steps, clamped 0-100) ──────────────────────────

  static void positionUp(int idx, bool& updated_ui) {
    int& p   = pos(idx);
    int newp = ((p < 0 ? 0 : p) + 10);
    if (newp > 100) newp = 100;
    p          = newp;
    st(idx)    = (p == 0) ? "closed" : "open";
    updated_ui = true;
  }

  static void positionDown(int idx, bool& updated_ui) {
    int& p   = pos(idx);
    int newp = ((p < 0 ? 100 : p) - 10);
    if (newp < 0) newp = 0;
    p          = newp;
    st(idx)    = (p == 0) ? "closed" : "open";
    updated_ui = true;
  }

  // ── helpers for YAML lambdas ───────────────────────────────────────────────

  static int getPosition(int idx) { return pos(idx); }

  static bool isClosed(int idx) {
    const std::string& s = st(idx);
    return s == "closed" || s == "stopped" || s == "unknown"
        || s == "unavailable" || s.empty();
  }

  // ── display ────────────────────────────────────────────────────────────────

  template<class D, class F>
  static void draw(D* it, F* symbols, F* state_icons,
                   F* font_big, F* font_base, F* font_small,
                   int selected_idx,
                   bool& updated_ui)
  {
    if (!updated_ui) return;
    updated_ui = false;

    const std::string& state = st(selected_idx);
    int position             = pos(selected_idx);

    it->clear();

    char label[12];
    strncpy(label, COVER_LIST[selected_idx].name, sizeof(label) - 1);
    label[sizeof(label) - 1] = '\0';
    for (int i = 0; label[i]; i++) label[i] = toupper((unsigned char)label[i]);

    // Curtain icon top-left
    it->print(5, 8, symbols, "");

    if (position < 0 || state == "unknown" || state == "unavailable" || state.empty()) {
      // Centred between icon right edge (~29 px) and screen right edge (127 px)
      it->print(78, 18, font_base, COLOR_ON, display::TextAlign::CENTER, "Unknown");
    } else {
      char pos_buf[5];
      snprintf(pos_buf, sizeof(pos_buf), "%d", position);
      it->print(64, 22, font_big, COLOR_ON, display::TextAlign::CENTER, pos_buf);

      int pct_x = (position < 10) ? 77 : (position < 100) ? 88 : 99;
      it->print(pct_x, 4, font_base, COLOR_ON, "%");

      it->print(110, 8, state_icons, stateIcon(state));
    }

    it->line(0, 38, 127, 38);
    it->print(64, 46, font_base, COLOR_ON, display::TextAlign::CENTER, label);

    draw_bottom_menu(it, font_small, "CLOSE", "", "OPEN");

    it->display();
  }

private:

  static std::string& st(int idx) {
    static bool init = false;
    static std::string states[COVER_LIST_COUNT];
    if (!init) {
      for (int i = 0; i < COVER_LIST_COUNT; i++) states[i] = "unknown";
      init = true;
    }
    return states[idx];
  }

  static int& pos(int idx) {
    static bool init = false;
    static int positions[COVER_LIST_COUNT];
    if (!init) {
      for (int i = 0; i < COVER_LIST_COUNT; i++) positions[i] = -1;
      init = true;
    }
    return positions[idx];
  }

  static const char* stateIcon(const std::string& state) {
    if (state == "open")    return "";  // \uef4a (opened)
    if (state == "closed")  return "";  // \ue5cd (closed)
    if (state == "opening") return "";  // \uf8ac (opening)
    if (state == "closing") return "";  // \uf8ab (closing)
    return                         "";  // \ue034 pause (stopped / fallback)
  }
};
