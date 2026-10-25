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
// Layout (everything below the strip centred on row CY = 31):
//   rows 0..8    context strip: "COVERS" chip, one dot per cover in the room
//                filter, the cover's room (its name without one) in the badge
//   x=5..28      24 px curtain icon, vertically centred; under it the state
//                in the 3x5 font, centred on x=17, rows 44..48 (OPEN /
//                CLOSED / OPENING / CLOSING / STOPPED, UNKNOWN before data)
//   x=38..106    name (font_base) beside the icon, vertically centred, two
//                lines split at the first space when it doesn't fit
//                (print_name_beside) - the same card as a dimmable light
//   x=117..125   9x39 slider (draw_vslider), rows 12..50, filled from the
//                bottom with the position; while moving, a tick marks the
//                target; "35%" (font_small) right-aligned at x=114 by its foot
//   y=53..63     CLOSE | ••• | OPEN; while moving, the key in that
//                direction reads STOP (covers_btn_open / _close stop it)
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
//                         id(font_bigger), id(font_base), id(font_small),
//                         id(selected_cover_index), id(selected_cover_name),
//                         id(updated_ui));

class CoverController {
public:

  static const int CY = 31;  // centre row of everything below the strip

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
    if (s != "opening" && s != "closing") tgt(sensor_idx) = -1;
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
    tgt(idx)   = p;
    updated_ui = true;
  }

  static void positionDown(int idx, bool& updated_ui) {
    int& p   = pos(idx);
    int newp = ((p < 0 ? 100 : p) - 10);
    if (newp < 0) newp = 0;
    p          = newp;
    st(idx)    = (p == 0) ? "closed" : "open";
    tgt(idx)   = p;
    updated_ui = true;
  }

  // ── movement target (for the slider tick while it moves) ──────────────────
  // Set when this remote commands a cover (open 100, close 0, a position);
  // cleared once the cover reports a state that isn't moving.
  static void setTarget(int idx, int target) { tgt(idx) = target; }

  static bool isOpening(int idx) { return st(idx) == "opening"; }
  static bool isClosing(int idx) { return st(idx) == "closing"; }

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
                   F* font_bigger, F* font_base, F* font_small,
                   int selected_idx,
                   bool& updated_ui, int conn_status)
  {
    if (!updated_ui) return;
    updated_ui = false;
    (void) state_icons;  // the state is a chip now; both kept so the YAML call is unchanged
    (void) font_bigger;

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

    // ── curtain icon, vertically centred (glyph rows 5..24 of the 24 px font),
    //    the state under it in the 3x5 font (font_small is too wide there for
    //    CLOSING / UNKNOWN)
    it->print(5, CY - 15, symbols, "\uec12");
    const bool known = !(position < 0 || state == "unknown" || state == "unavailable" || state.empty());
    const char* word = known ? stateWord(state) : "UNKNOWN";
    const int   sx   = 17 - ContextStrip::textWidth(word) / 2;
    ContextStrip::text(it, sx < 1 ? 1 : sx, 44, word, COLOR_ON);

    // ── name beside the icon ───────────────────────────────────────────────────
    char name[16];
    strncpy(name, COVER_LIST[ci].name, sizeof(name) - 1);
    name[sizeof(name) - 1] = '\0';
    for (int k = 0; name[k]; k++) name[k] = toupper((unsigned char) name[k]);
    print_name_beside(it, font_base, font_small, name, 38, CY, VSLIDER_TEXT_R - 8);  // x=38 like the lights card

    // ── slider on the right: position, plus the target tick while moving ──────
    int tick = -1;
    if (known && (state == "opening" || state == "closing")) {
      tick = tgt(ci);
      if (tick < 0) tick = (state == "opening") ? 100 : 0;
    }
    draw_vslider(it, known ? position : -1, tick);
    if (known) {
      char pct[6];
      snprintf(pct, sizeof(pct), "%d%%", position);
      it->print(VSLIDER_TEXT_R, 47, font_small, COLOR_ON, display::TextAlign::CENTER_RIGHT, pct);
    }

    draw_bottom_menu(it, font_small, state == "closing" ? "STOP" : "CLOSE", nullptr,
                     state == "opening" ? "STOP" : "OPEN");

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

  static const char* stateWord(const std::string& state) {
    if (state == "open")    return "OPEN";
    if (state == "closed")  return "CLOSED";
    if (state == "opening") return "OPENING";
    if (state == "closing") return "CLOSING";
    return                         "STOPPED";
  }

  // Target of the current movement as set by this remote, or -1.
  static int& tgt(int idx) {
    static bool init = false;
    static int targets[COVER_LIST_CAPACITY];
    if (!init) {
      for (int i = 0; i < COVER_LIST_CAPACITY; i++) targets[i] = -1;
      init = true;
    }
    return targets[wrap_index(idx, COVER_LIST_CAPACITY)];
  }
};
