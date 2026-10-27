#pragma once
#include <string>
#include <cstring>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <algorithm>
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
//   x=36..114    name (font_base) beside the icon, vertically centred, two
//                lines split at the first space when it doesn't fit
//                (print_name_beside) - the same card as a dimmable light
//   x=117..125   9x39 slider (draw_vslider), rows 12..50, filled from the
//                bottom with the position; while moving, a tick marks the
//                target; "35%" (font_small) right-aligned at x=114 by its foot
//   y=53..63     OPEN | ••• | CLOSE; while moving, the key in that
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
//   CoverController::endEdit(id(selected_cover_index), commit, id(updated_ui));
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
    reported(sensor_idx) = ha_pos;
    // While position presses are being collected the shown value is the one being
    // set; a moving cover's reports must not drag it around (see endEdit).
    if (editing(sensor_idx)) return;
    int& p = pos(sensor_idx);
    if (p == ha_pos) return;
    p = ha_pos;
    updated_ui = true;
  }

  // ── position control (5 % steps, clamped 0-100) ───────────────────────────
  // A cover stopped mid-way can sit at any value (26, 38 …); the first press
  // snaps it to the next multiple of 5 in the pressed direction (26 → 30 up,
  // 26 → 25 down), later presses move 5 at a time. The press only sets the
  // shown value; covers_do_position sends it once the presses stop.

  static void positionUp(int idx, bool& updated_ui) {
    int p = pos(idx);
    if (p < 0) p = 0;
    setEdited(idx, std::min(100, (p / 5 + 1) * 5), updated_ui);
  }

  static void positionDown(int idx, bool& updated_ui) {
    int p = pos(idx);
    if (p < 0) p = 100;
    setEdited(idx, std::max(0, ((p + 4) / 5 - 1) * 5), updated_ui);
  }

  // The collected position was sent (commit) or dropped for OPEN / CLOSE / STOP:
  // reports from the cover drive the shown value again. Dropping it puts the
  // last reported position back on screen.
  static void endEdit(int idx, bool commit, bool& updated_ui) {
    if (!editing(idx)) return;
    editing(idx) = false;
    if (!commit) {
      pos(idx)   = reported(idx);
      tgt(idx)   = -1;
      updated_ui = true;
    }
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
    // x=36 like the lights card; up to the slider - the % sits below the name rows
    print_name_beside(it, font_base, font_small, name, 36, CY, VSLIDER_TEXT_R);

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

    draw_bottom_menu(it, font_small, state == "opening" ? "STOP" : "OPEN", nullptr,
                     state == "closing" ? "STOP" : "CLOSE");

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

  static void setEdited(int idx, int p, bool& updated_ui) {
    pos(idx)     = p;
    st(idx)      = (p == 0) ? "closed" : "open";
    tgt(idx)     = p;
    editing(idx) = true;
    updated_ui   = true;
  }

  // True between a position press and its send (or its cancel).
  static bool& editing(int idx) {
    static bool flags[COVER_LIST_CAPACITY] = {};
    return flags[wrap_index(idx, COVER_LIST_CAPACITY)];
  }

  // Last position the cover reported, kept while editing hides it.
  static int& reported(int idx) {
    static bool init = false;
    static int values[COVER_LIST_CAPACITY];
    if (!init) {
      for (int i = 0; i < COVER_LIST_CAPACITY; i++) values[i] = -1;
      init = true;
    }
    return values[wrap_index(idx, COVER_LIST_CAPACITY)];
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
