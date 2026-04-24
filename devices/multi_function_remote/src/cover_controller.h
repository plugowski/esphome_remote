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
// Button mapping:
//   LEFT  (pin14)    -> prevCover
//   RIGHT (pin26)    -> actionStop
//   MODE  (pin22)    -> actionClose
//   FAN   (pin33)    -> actionOpen
//   MENU  (pin5)     -> opens mode menu (single click, same as all other modes)
//   T_UP  (pin23/17) -> positionUp
//   T_DN  (pin19/18) -> positionDown
//
// YAML usage (one-liners):
//   CoverController::prevCover(id(selected_cover_index));
//   CoverController::nextCover(id(selected_cover_index));
//   CoverController::selectCover(id(selected_cover_index),
//                                id(selected_cover), id(selected_cover_name));
//   CoverController::actionOpen(id(cover_position), id(cover_state), id(updated_ui));
//   CoverController::actionClose(id(cover_position), id(cover_state), id(updated_ui));
//   CoverController::actionStop(id(cover_state), id(updated_ui));
//   CoverController::positionUp(id(cover_position), id(cover_state), id(updated_ui));
//   CoverController::positionDown(id(cover_position), id(cover_state), id(updated_ui));
//   CoverController::syncState(ha_state, ha_pos,
//                              id(cover_state), id(cover_position), id(updated_ui));
//   CoverController::draw(id(oled_display),
//                         id(ac_symbols), id(menu_icon_small),
//                         id(font_big), id(font_base), id(font_small),
//                         id(selected_cover_name), id(cover_state),
//                         id(cover_position), id(updated_ui));

class CoverController {
public:

  // ── navigation ─────────────────────────────────────────────────────────────

  static void prevCover(int& idx) {
    idx = (idx - 1 + COVER_LIST_COUNT) % COVER_LIST_COUNT;
  }

  static void nextCover(int& idx) {
    idx = (idx + 1) % COVER_LIST_COUNT;
  }

  static void selectCover(int idx, std::string& entity, std::string& name) {
    entity = COVER_LIST[idx].entity_id;
    name   = COVER_LIST[idx].name;
  }

  // ── position control (10 % steps, clamped 0-100) ──────────────────────────

  static void positionUp(int& position, std::string& state, bool& updated_ui) {
    int p      = (position < 0) ? 0 : position;
    position   = (p + 10 <= 100) ? p + 10 : 100;
    state      = (position == 0) ? std::string("closed") : std::string("open");
    updated_ui = true;
  }

  static void positionDown(int& position, std::string& state, bool& updated_ui) {
    int p      = (position < 0) ? 100 : position;
    position   = (p - 10 >= 0) ? p - 10 : 0;
    state      = (position == 0) ? std::string("closed") : std::string("open");
    updated_ui = true;
  }

  // ── direct actions ─────────────────────────────────────────────────────────

  static void actionOpen(int& position, std::string& state, bool& updated_ui) {
    position   = 100;
    state      = "open";
    updated_ui = true;
  }

  static void actionClose(int& position, std::string& state, bool& updated_ui) {
    position   = 0;
    state      = "closed";
    updated_ui = true;
  }

  static void actionStop(std::string& state, bool& updated_ui) {
    state      = "stopped";
    updated_ui = true;
  }

  // ── HA state sync ──────────────────────────────────────────────────────────

  // Called from each state text_sensor's on_value.
  // No-op when sensor_idx does not match the currently selected cover.
  static void syncCoverState(int sensor_idx, const std::string& ha_state,
                              int selected_idx, std::string& state, bool& updated_ui) {
    if (sensor_idx != selected_idx || ha_state.empty() || state == ha_state) return;
    state      = ha_state;
    updated_ui = true;
  }

  // Called from each position sensor's on_value.
  // No-op when sensor_idx does not match the currently selected cover.
  static void syncCoverPosition(int sensor_idx, float ha_pos_f,
                                 int selected_idx, int& position, bool& updated_ui) {
    if (sensor_idx != selected_idx) return;
    int ha_pos = (std::isnan(ha_pos_f) || ha_pos_f < 0) ? -1 : static_cast<int>(ha_pos_f);
    if (position == ha_pos) return;
    position   = ha_pos;
    updated_ui = true;
  }

  // Combined sync — convenience for cases where both values arrive together.
  static void syncState(const std::string& ha_state, float ha_pos_f,
                        std::string& state, int& position, bool& updated_ui) {
    bool changed = false;
    if (!ha_state.empty() && state != ha_state) {
      state   = ha_state;
      changed = true;
    }
    int ha_pos = (std::isnan(ha_pos_f) || ha_pos_f < 0)
                   ? -1 : static_cast<int>(ha_pos_f);
    if (position != ha_pos) {
      position = ha_pos;
      changed  = true;
    }
    if (changed) updated_ui = true;
  }

  // ── display ────────────────────────────────────────────────────────────────

  // Renders the cover screen.  No-ops when updated_ui is false.
  //
  // Layout (128 x 64 OLED) -- mirrors AC cool mode structure:
  //   top-left  : curtain icon 24 px  (symbols, same position as AC unit icon)
  //   centre    : position "75%"      (font_big,  same as AC temperature)
  //   top-right : state icon 18 px    (state_icons, same corner as AC fan bars)
  //   y=38      : separator line
  //   y=38-52   : room name centred   (font_base, same as AC unit name bar)
  //   y=53-63   : CLOSE | HOLD | OPEN
  //
  // symbols must include '\uec12' (curtain).
  // state_icons must include the five cover-state glyphs (see stateIcon()).
  // font_big must include '%'.
  template<class D, class F>
  static void draw(D* it, F* symbols, F* state_icons,
                   F* font_big, F* font_base, F* font_small,
                   const std::string& cover_name,
                   const std::string& state, int position,
                   bool& updated_ui)
  {
    if (!updated_ui) return;
    updated_ui = false;

    it->clear();

    char label[12];
    strncpy(label, cover_name.c_str(), sizeof(label) - 1);
    label[sizeof(label) - 1] = '\0';
    for (int i = 0; label[i]; i++) label[i] = toupper((unsigned char)label[i]);

    // ── content area (y=0-37) ─────────────────────────────────────────────────
    // Curtain icon top-left -- same position as AC unit/fan/dry icons
    it->print(5, 8, symbols, "\uec12");

    if (position < 0 || state == "unknown") {
      it->print(64, 18, font_base, COLOR_ON, display::TextAlign::CENTER, "Unknown");
    } else {
      // Big position number centred -- mirrors AC temperature display
      char pos_buf[5];
      snprintf(pos_buf, sizeof(pos_buf), "%d", position);
      it->print(64, 22, font_big, COLOR_ON, display::TextAlign::CENTER, pos_buf);

      // "%" superscript -- mirrors AC "C" degree sign; x shifts with digit count
      int pct_x = (position < 10) ? 77 : (position < 100) ? 88 : 99;
      it->print(pct_x, 4, font_base, COLOR_ON, "%");

      // State icon top-right -- mirrors AC fan-speed bars corner
      it->print(110, 8, state_icons, stateIcon(state));
    }

    // ── room name bar (mirrors AC lower bar) ──────────────────────────────────
    it->line(0, 38, 127, 38);
    it->print(64, 46, font_base, COLOR_ON, display::TextAlign::CENTER, label);

    // ── bottom menu: pin22=CLOSE  pin5=HOLD->STOP  pin33=OPEN ────────────────
    draw_bottom_menu(it, font_small, "CLOSE", "", "OPEN");

    it->display();
  }

private:

  // Maps HA cover state string to a Material Symbols glyph (18 px state_icons font).
  static const char* stateIcon(const std::string& state) {
    if (state == "open")    return "\ue5d8";  // open_in_full
    if (state == "closed")  return "\ue5d1";  // close_fullscreen
    if (state == "opening") return "\ue5ce";  // unfold_more
    if (state == "closing") return "\ue5cc";  // unfold_less
    return                         "\ue034";  // pause  (stopped / fallback)
  }
};
