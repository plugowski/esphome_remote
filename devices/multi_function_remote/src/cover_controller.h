#pragma once
#include <string>
#include <cstring>
#include <cctype>
#include <cmath>
#include <cstdio>
#include "cover_entities.h"
#include "display_utils.h"
#include "remote_core.h"
#include "room_entities.h"
#include "room_filter.h"

// ── CoverController ───────────────────────────────────────────────────────────
// Cover mode: entity selection, open/close/stop/position actions, display.
//
// State and position for every cover are stored in private static arrays so
// that switching covers immediately shows the last received values instead of
// reverting to Unknown while waiting for HA to re-push data.
//
// Layout (same frame as the climate screen):
//   rows 0..8    context strip: "COVERS" chip, one dot per cover in the room
//                filter, the cover's room (its name without one) in the badge
//   hero row     centred on HERO_CY = 29: 24 px curtain icon top-left at
//                (5, 14); position in font_big centred at (64, 30) with "%"
//                in font_base after it; 18 px state icon centred at (116, 29)
//                (open / closed / opening / closing / stopped)
//   y=53..63     CLOSE | ••• | OPEN
// Prev / next skip covers outside the room filter; a room with no cover in
// it shows the empty-room screen (room_filter.h).
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

  static const int HERO_CY = 29;  // centre row of the hero, below the context strip

  // ── navigation ─────────────────────────────────────────────────────────────

  static void prevCover(int& idx) {
    idx = RoomFilter::step(idx, COVER_LIST_COUNT, -1, coverRoom);
  }

  static void nextCover(int& idx) {
    idx = RoomFilter::step(idx, COVER_LIST_COUNT, +1, coverRoom);
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
                   bool& updated_ui, int conn_status)
  {
    if (!updated_ui) return;
    updated_ui = false;

    // Nothing in the chosen room: empty state (the keys are gated off too)
    if (COVER_LIST_COUNT > 0 && !RoomFilter::any(COVER_LIST_COUNT, coverRoom)) {
      drawEmptyRoom(it, font_base, font_small, "COVERS", "covers", conn_status);
      return;
    }

    const int ci             = wrap_index(selected_idx, COVER_LIST_CAPACITY);
    const std::string& state = st(ci);
    const int position       = pos(ci);

    it->clear();

    // ── context strip: "COVERS" chip, one dot per cover in the room filter;
    //    the badge names the cover's room, or the cover itself without one ──
    const char* room = roomName(COVER_LIST[ci].room);
    ContextStrip::draw(it, "COVERS",
                       RoomFilter::position(ci, COVER_LIST_COUNT, coverRoom),
                       RoomFilter::visibleCount(COVER_LIST_COUNT, coverRoom), conn_status,
                       room ? room : COVER_LIST[ci].name);

    // ── hero row ──────────────────────────────────────────────────────────────
    it->print(5, HERO_CY - 15, symbols, "\uec12");   // curtain

    if (position < 0 || state == "unknown" || state == "unavailable" || state.empty()) {
      it->print(72, HERO_CY, font_base, COLOR_ON, display::TextAlign::CENTER, "UNKNOWN");
    } else {
      char pos_buf[5];
      snprintf(pos_buf, sizeof(pos_buf), "%d", position);
      int bx, by, bw, bh;
      it->get_text_bounds(64, HERO_CY + 1, pos_buf, font_big, display::TextAlign::CENTER, &bx, &by, &bw, &bh);
      it->print(64, HERO_CY + 1, font_big, COLOR_ON, display::TextAlign::CENTER, pos_buf);
      it->print(bx + bw + 1, HERO_CY - 17, font_base, COLOR_ON, "%");
      it->print(116, HERO_CY, state_icons, COLOR_ON, display::TextAlign::CENTER, stateIcon(state));
    }

    draw_bottom_menu(it, font_small, "CLOSE", nullptr, "OPEN");

    Toast::present(it);
  }

private:

  static std::string& st(int idx) {
    static bool init = false;
    static std::string states[COVER_LIST_CAPACITY];
    if (!init) {
      for (int i = 0; i < COVER_LIST_CAPACITY; i++) states[i] = "unknown";
      init = true;
    }
    return states[wrap_index(idx, COVER_LIST_CAPACITY)];
  }

  static int& pos(int idx) {
    static bool init = false;
    static int positions[COVER_LIST_CAPACITY];
    if (!init) {
      for (int i = 0; i < COVER_LIST_CAPACITY; i++) positions[i] = -1;
      init = true;
    }
    return positions[wrap_index(idx, COVER_LIST_CAPACITY)];
  }

  static const char* stateIcon(const std::string& state) {
    if (state == "open")    return "";  // \uef4a (opened)
    if (state == "closed")  return "";  // \ue5cd (closed)
    if (state == "opening") return "";  // \uf8ac (opening)
    if (state == "closing") return "";  // \uf8ab (closing)
    return                         "";  // \ue034 pause (stopped / fallback)
  }
};
