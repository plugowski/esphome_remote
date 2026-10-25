#pragma once
#include <cctype>
#include <cstdio>
#include "display_utils.h"

// ── ContextStrip ─────────────────────────────────────────────────────────────
// Status line across the top of a screen, telling the user where they are:
//
//   ╭───────╮                       ╭───────╮
//   │CLIMATE│ ▪█▪            20:25 │BEDROOM│ [▮▮▮ ]
//   ╰───────╯                       ╰───────╯
//   rows 0..8 — screen content starts at ContextStrip::CONTENT_Y (10)
//
//   x=0..        inverted chip naming the screen ("MENU", "LIGHTS", "CLIMATE", ...)
//   after chip   position dots: one per page, the current one a 5x5 block. When they
//                don't fit before the clock, falls back to "3/12" text.
//   ..x=114      optional inverted context badge: which room / unit the screen
//                is showing ("BEDROOM", or "ALL" while there's no room filter)
//                Both chips have their corner pixels cut, for a rounded look.
//   left of it   clock (HH:MM); replaced by NO WIFI / NO HA / NO HUB while
//                conn_status != 0. Hidden until time is valid (always hidden
//                on the ESP-NOW build, which has no clock).
//   x=117..127   battery outline with a 5-step level; outline only while the
//                level is unknown (battery_pct == -1).
//
// Text uses a built-in 3x5 pixel font (caps, digits, ":/%-."), so the strip
// needs no extra font component and stays legible at 5 px.
//
// Clock and battery are pushed in from YAML, so controllers don't need extra
// draw() arguments:
//   ContextStrip::setBattery(id(battery_pct));              // on each reading
//   if (ContextStrip::setClock(t.is_valid(), t.hour, t.minute))
//     id(updated_ui) = true;                                // minute rolled over
//
// Drawing (from a controller's draw(), after it->clear()):
//   ContextStrip::draw(it, "LIGHTS", page, page_count, conn_status, "ALL");

class ContextStrip {
public:
  static const int HEIGHT    = 9;   // rows 0..8
  static const int CONTENT_Y = 10;  // first free row below the strip

  // Returns true when the visible clock text changed (minute rolled over, or
  // time became valid), so the caller can request a redraw.
  static bool setClock(bool valid, int hour, int minute) {
    const int v = valid ? hour * 60 + minute : -1;
    if (clockValue() == v) return false;
    clockValue() = v;
    return true;
  }

  // 0..100, or -1 while unknown.
  static void setBattery(int pct) { batteryValue() = pct; }

  // ── tiny 3x5 font ─────────────────────────────────────────────────────────

  static int textWidth(const char* s) {
    int w = 0;
    for (const char* p = s; *p; p++) w += glyph(*p).w + 1;
    return w > 0 ? w - 1 : 0;
  }

  template<class D>
  static int text(D* it, int x, int y, const char* s, Color c) {
    for (const char* p = s; *p; p++) {
      const Glyph& g = glyph(*p);
      for (int r = 0; r < 5; r++)
        for (int col = 0; col < g.w; col++)
          if (g.rows[r] & (1 << (g.w - 1 - col))) it->draw_pixel_at(x + col, y + r, c);
      x += g.w + 1;
    }
    return x;
  }

  // ── vertical paginator ────────────────────────────────────────────────────
  // For lists navigated with up/down: the same dots as the strip's, stacked at
  // the right edge and centred on rows 10..50 - 2 px clear of the soft buttons
  // (row 53), level with the bottom of draw_vslider. Current item is a 5x5
  // block (x=123..127), others 3x2 (x=124..126), 2 px apart. When they don't
  // fit (more than 10), a 1 px track with a 5 px wide thumb at x=125 instead.
  template<class D>
  static void drawVerticalDots(D* it, int pos, int count) {
    if (count <= 1) return;
    const int cur   = wrap_index(pos, count);
    const int top   = CONTENT_Y, bottom = 50;  // 2 px clear of the buttons (row 53)
    const int total = (count - 1) * 4 + 5;
    if (total <= bottom - top + 1) {
      int y = (top + bottom + 1) / 2 - total / 2;
      for (int i = 0; i < count; i++) {
        if (i == cur) it->filled_rectangle(123, y, 5, 5, COLOR_ON);
        else          it->filled_rectangle(124, y, 3, 2, COLOR_ON);
        y += ((i == cur) ? 5 : 2) + 2;
      }
    } else {
      const int span  = bottom - top + 1;
      const int thumb = (span / count < 3) ? 3 : span / count;
      const int y     = top + (span - thumb) * cur / (count - 1);
      it->filled_rectangle(125, top, 1, span, COLOR_ON);
      it->filled_rectangle(123, y, 5, thumb, COLOR_ON);
    }
  }

  // Inverted label in the 3x5 font with its four corner pixels cut, 9 rows
  // tall from row y (the strip's chips use y=0). Returns its width.
  template<class D>
  static int chip(D* it, int x, const char* s, int y = 0) {
    const int w = textWidth(s) + 4;
    it->filled_rectangle(x, y, w, HEIGHT, COLOR_ON);
    it->draw_pixel_at(x,         y,              COLOR_OFF);
    it->draw_pixel_at(x + w - 1, y,              COLOR_OFF);
    it->draw_pixel_at(x,         y + HEIGHT - 1, COLOR_OFF);
    it->draw_pixel_at(x + w - 1, y + HEIGHT - 1, COLOR_OFF);
    text(it, x + 2, y + 2, s, COLOR_OFF);
    return w;
  }

  // ── strip ─────────────────────────────────────────────────────────────────

  template<class D>
  static void draw(D* it, const char* label, int pos, int count, int conn_status,
                   const char* context = nullptr, bool battery_pct = false) {
    // label chip
    int x = 0;
    if (label && label[0]) x = chip(it, 0, label) + 4;

    // right side, from the edge inwards: battery, context badge, link status
    drawBattery(it, 117, 1);
    int right_edge = 114;
    // battery_pct: the level as "87%" left of the icon (once it's known)
    if (battery_pct && batteryValue() >= 0) {
      char pct[6];
      snprintf(pct, sizeof(pct), "%d%%", batteryValue());
      const int w = textWidth(pct);
      text(it, right_edge - w, 2, pct, COLOR_ON);
      right_edge -= w + 3;
    }
    if (context && context[0]) {
      const int w = textWidth(context) + 4;
      chip(it, right_edge - w, context);
      right_edge -= w + 3;
    }

    char right[10] = "";
    if      (conn_status == 1) snprintf(right, sizeof(right), "NO WIFI");
    else if (conn_status == 2) snprintf(right, sizeof(right), "NO HA");
    else if (conn_status == 3) snprintf(right, sizeof(right), "NO HUB");
    else if (clockValue() >= 0)
      snprintf(right, sizeof(right), "%d:%02d", clockValue() / 60, clockValue() % 60);
    const int right_x = right_edge - textWidth(right);
    if (right[0]) text(it, right_x, 2, right, COLOR_ON);

    // position: dots when they fit, "n/N" otherwise
    if (count <= 1) return;
    const int cur   = wrap_index(pos, count);
    const int limit = (right[0] ? right_x : right_edge) - 4;
    const int dots_w = (count - 1) * 4 + 5;  // 2 px dots + 2 px gaps, current 5 px
    if (x + dots_w <= limit) {
      for (int i = 0; i < count; i++) {
        // current page: 5x5 (rows 2..6, the text height); others 2x3 (rows 3..5)
        if (i == cur) it->filled_rectangle(x, 2, 5, 5, COLOR_ON);
        else          it->filled_rectangle(x, 3, 2, 3, COLOR_ON);
        x += ((i == cur) ? 5 : 2) + 2;
      }
    } else {
      char buf[8];
      snprintf(buf, sizeof(buf), "%d/%d", cur + 1, count);
      if (x + textWidth(buf) <= limit) text(it, x, 2, buf, COLOR_ON);
    }
  }

private:
  struct Glyph { char c; uint8_t w; uint8_t rows[5]; };

  static int& clockValue()   { static int v = -1; return v; }
  static int& batteryValue() { static int v = -1; return v; }

  template<class D>
  static void drawBattery(D* it, int x, int y) {
    it->rectangle(x, y, 9, 7, COLOR_ON);
    it->filled_rectangle(x + 9, y + 2, 1, 3, COLOR_ON);
    const int pct = batteryValue();
    if (pct < 0) return;
    int bars = (pct * 5 + 50) / 100;
    if (bars < 1 && pct > 0) bars = 1;
    if (bars > 5) bars = 5;
    if (bars > 0) it->filled_rectangle(x + 2, y + 2, bars, 3, COLOR_ON);
  }

  static const Glyph& glyph(char ch) {
    static const Glyph TABLE[] = {
      {' ', 2, {0, 0, 0, 0, 0}},
      {'0', 3, {0b111, 0b101, 0b101, 0b101, 0b111}},
      {'1', 3, {0b010, 0b110, 0b010, 0b010, 0b111}},
      {'2', 3, {0b110, 0b001, 0b010, 0b100, 0b111}},
      {'3', 3, {0b110, 0b001, 0b010, 0b001, 0b110}},
      {'4', 3, {0b101, 0b101, 0b111, 0b001, 0b001}},
      {'5', 3, {0b111, 0b100, 0b110, 0b001, 0b110}},
      {'6', 3, {0b011, 0b100, 0b111, 0b101, 0b111}},
      {'7', 3, {0b111, 0b001, 0b010, 0b010, 0b010}},
      {'8', 3, {0b111, 0b101, 0b111, 0b101, 0b111}},
      {'9', 3, {0b111, 0b101, 0b111, 0b001, 0b110}},
      {'A', 3, {0b010, 0b101, 0b111, 0b101, 0b101}},
      {'B', 3, {0b110, 0b101, 0b110, 0b101, 0b110}},
      {'C', 3, {0b011, 0b100, 0b100, 0b100, 0b011}},
      {'D', 3, {0b110, 0b101, 0b101, 0b101, 0b110}},
      {'E', 3, {0b111, 0b100, 0b110, 0b100, 0b111}},
      {'F', 3, {0b111, 0b100, 0b110, 0b100, 0b100}},
      {'G', 3, {0b011, 0b100, 0b101, 0b101, 0b011}},
      {'H', 3, {0b101, 0b101, 0b111, 0b101, 0b101}},
      {'I', 3, {0b111, 0b010, 0b010, 0b010, 0b111}},
      {'J', 3, {0b001, 0b001, 0b001, 0b101, 0b010}},
      {'K', 3, {0b101, 0b101, 0b110, 0b101, 0b101}},
      {'L', 3, {0b100, 0b100, 0b100, 0b100, 0b111}},
      {'M', 5, {0b10001, 0b11011, 0b10101, 0b10001, 0b10001}},
      {'N', 4, {0b1001, 0b1101, 0b1011, 0b1001, 0b1001}},
      {'O', 3, {0b010, 0b101, 0b101, 0b101, 0b010}},
      {'P', 3, {0b110, 0b101, 0b110, 0b100, 0b100}},
      {'Q', 3, {0b010, 0b101, 0b101, 0b110, 0b011}},
      {'R', 3, {0b110, 0b101, 0b110, 0b101, 0b101}},
      {'S', 3, {0b011, 0b100, 0b010, 0b001, 0b110}},
      {'T', 3, {0b111, 0b010, 0b010, 0b010, 0b010}},
      {'U', 3, {0b101, 0b101, 0b101, 0b101, 0b111}},
      {'V', 3, {0b101, 0b101, 0b101, 0b101, 0b010}},
      {'W', 5, {0b10001, 0b10001, 0b10101, 0b11011, 0b10001}},
      {'X', 3, {0b101, 0b101, 0b010, 0b101, 0b101}},
      {'Y', 3, {0b101, 0b101, 0b010, 0b010, 0b010}},
      {'Z', 3, {0b111, 0b001, 0b010, 0b100, 0b111}},
      {':', 1, {0b0, 0b1, 0b0, 0b1, 0b0}},
      {'/', 3, {0b001, 0b001, 0b010, 0b100, 0b100}},
      {'%', 3, {0b101, 0b001, 0b010, 0b100, 0b101}},
      {'-', 3, {0b000, 0b000, 0b111, 0b000, 0b000}},
      {'.', 1, {0b0, 0b0, 0b0, 0b0, 0b1}},
    };
    const char up = static_cast<char>(toupper(static_cast<unsigned char>(ch)));
    for (const Glyph& g : TABLE)
      if (g.c == up) return g;
    return TABLE[0];  // unknown → blank space
  }
};
