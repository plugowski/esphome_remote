#pragma once
#include <cctype>
#include <cstdio>
#include "room_entities.h"
#include "ac_entities.h"
#include "light_entities.h"
#include "cover_entities.h"
#include "automation_entities.h"
#include "display_utils.h"
#include "context_strip.h"

// ── Room filter ───────────────────────────────────────────────────────────────
// A long press on the menu key opens the room picker; the room chosen there
// narrows the screens that browse entities (climate units, lights) to that
// room. 0 = all rooms.
//
// The picker runs as its own screen: app_mode is set to APP_ROOMS while it is
// open (the mode it came from is kept in room_picker_return), so every
// existing per-mode button branch simply doesn't match and the picker gets its
// own branches next to them. See packages/menu.yaml.
//
// A list with nothing in the chosen room is shown unfiltered, so a screen is
// never left empty with its keys acting on an entity you can't see.
//
// YAML usage:
//   RoomFilter::current() = id(room_filter);                    // on boot
//   id(selected_light_index) = RoomFilter::first(id(selected_light_index),
//                                                LIGHTS_LIST_COUNT, lightRoom);
//   RoomPicker::draw(id(oled_display), id(font_small), id(room_picker_index),
//                    id(conn_status));

static const int APP_ROOMS = 100;  // app_mode while the room picker is open

inline int acRoom(int i)         { return AC_LIST[i].room; }
inline int lightRoom(int i)      { return LIGHTS_LIST[i].room; }
inline int coverRoom(int i)      { return COVER_LIST[i].room; }
inline int automationRoom(int i) { return AUTOMATION_LIST[i].room; }

class RoomFilter {
public:
  // 0 = all rooms, 1..ROOM_LIST_COUNT = that room. Mirrors the room_filter
  // global (restored across deep sleep), set on boot and by the picker.
  static int& current() { static int v = 0; return v; }

  static bool active() { return current() >= 1 && current() <= ROOM_LIST_COUNT; }

  static const char* name() { return active() ? ROOM_LIST[current() - 1].name : nullptr; }

  // True when the filter narrows this list: a room is chosen and at least
  // one entry is in it.
  template<class F>
  static bool applies(int count, F roomOf) {
    if (!active()) return false;
    for (int i = 0; i < count; i++)
      if (roomOf(i) == current()) return true;
    return false;
  }

  template<class F>
  static bool visible(int idx, int count, F roomOf) {
    return !applies(count, roomOf) || roomOf(idx) == current();
  }

  // Next visible index from idx in direction dir (+1 / -1), wrapping; idx
  // itself when nothing else is visible.
  template<class F>
  static int step(int idx, int count, int dir, F roomOf) {
    for (int k = 1; k <= count; k++) {
      const int j = wrap_index(idx, count, dir * k);
      if (visible(j, count, roomOf)) return j;
    }
    return wrap_index(idx, count);
  }

  // idx if it's visible, otherwise the next visible one.
  template<class F>
  static int first(int idx, int count, F roomOf) {
    if (count <= 0) return 0;
    idx = wrap_index(idx, count);
    return visible(idx, count, roomOf) ? idx : step(idx, count, +1, roomOf);
  }

  // Visible entries, and idx's position among them (for the strip's dots).
  template<class F>
  static int visibleCount(int count, F roomOf) {
    int n = 0;
    for (int i = 0; i < count; i++) if (visible(i, count, roomOf)) n++;
    return n;
  }

  template<class F>
  static int position(int idx, int count, F roomOf) {
    int n = 0;
    for (int i = 0; i < count && i < idx; i++) if (visible(i, count, roomOf)) n++;
    return n;
  }

  // Index of the pos-th visible entry, or -1.
  template<class F>
  static int at(int pos, int count, F roomOf) {
    for (int i = 0; i < count; i++)
      if (visible(i, count, roomOf) && pos-- == 0) return i;
    return -1;
  }

  // Entities assigned to a room across every list (0 = all configured).
  static int entitiesIn(int room) {
    int n = 0;
    n += countIn(AC_LIST_COUNT, acRoom, room);
    n += countIn(LIGHTS_LIST_COUNT, lightRoom, room);
    n += countIn(COVER_LIST_COUNT, coverRoom, room);
    n += countIn(AUTOMATION_LIST_COUNT, automationRoom, room);
    return n;
  }

private:
  template<class F>
  static int countIn(int count, F roomOf, int room) {
    if (room == 0) return count;
    int n = 0;
    for (int i = 0; i < count; i++) if (roomOf(i) == room) n++;
    return n;
  }
};

// ── RoomPicker ────────────────────────────────────────────────────────────────
// Entries: 0 = ALL ROOMS, 1..ROOM_LIST_COUNT = the rooms, each with its
// entity count. Same 4-row, 10 px list as the lights list, a page at a time.
//
//   rows 0..8    context strip: "ROOM" chip, one dot per page of 4
//   rows 11..50  4 rows: name (font_small, x=4), count right-aligned at x=124;
//                the cursor row is inverted, the active filter gets a 2 px
//                bar at x=0
//   y=53..63     ▲ | OK | ▼

class RoomPicker {
public:
  static const int ROWS  = 4;
  static const int ROW_H = 10;

  static int entryCount() { return 1 + ROOM_LIST_COUNT; }

  static void up(int& idx)   { idx = wrap_index(idx, entryCount(), -1); }
  static void down(int& idx) { idx = wrap_index(idx, entryCount(), +1); }

  template<class D, class F>
  static void draw(D* it, F* font_small, int cursor, int conn_status) {
    it->clear();
    const int n          = entryCount();
    const int cur        = wrap_index(cursor, n);
    const int page       = cur / ROWS;
    const int page_count = (n + ROWS - 1) / ROWS;
    ContextStrip::draw(it, "ROOM", page, page_count, conn_status);

    for (int row = 0; row < ROWS; row++) {
      const int e = page * ROWS + row;
      if (e >= n) break;
      const int y_top    = ContextStrip::CONTENT_Y + 1 + row * ROW_H;  // rows 11..50
      const int y_center = y_top + 4;
      Color fg = COLOR_ON;
      if (e == cur) {
        it->filled_rectangle(0, y_top, 128, ROW_H, COLOR_ON);
        fg = COLOR_OFF;
      }
      if (e == RoomFilter::current()) it->filled_rectangle(0, y_top + 1, 2, ROW_H - 2, fg);

      char name[16];
      snprintf(name, sizeof(name), "%s", e == 0 ? "ALL ROOMS" : ROOM_LIST[e - 1].name);
      for (int k = 0; name[k]; k++) name[k] = toupper((unsigned char) name[k]);
      it->print(4, y_center, font_small, fg, display::TextAlign::CENTER_LEFT, name);

      char cnt[6];
      snprintf(cnt, sizeof(cnt), "%d", RoomFilter::entitiesIn(e));
      it->print(124, y_center, font_small, fg, display::TextAlign::CENTER_RIGHT, cnt);
    }

    draw_bottom_menu(it, font_small, "▲", "OK", "▼");
    Toast::present(it);
  }
};
