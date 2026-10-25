#pragma once
#include <cstdint>
#include <cstring>
#include "esphome/core/hal.h"
#include "esphome/components/display/display.h"
#include "esphome/components/font/font.h"

// ── Toast ─────────────────────────────────────────────────────────────────────
// A short notification drawn on top of whatever screen is showing, for a set
// time ("BATTERY 9%", later things like "TRIGGERED"). Any key closes it early;
// that press does nothing else (RemoteCore::onUserActivity consumes it).
//
//        ▛▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▜
//        ▌ ┌──────────────────────────┐ ▐   x=8..119, centred on row 31: a white
//        ▌ │ [icon]  BATTERY 9%       │ ▐   box with a black inner frame, black
//        ▌ │         CHARGE SOON      │ ▐   content, 2 px of black clearing round it
//        ▌ └──────────────────────────┘ ▐   title: font_base (font_small if it
//        ▙▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▟          doesn't fit); body: font_small,
//                                                  optional; icon: optional, 16 px
//
// Every controller sends its frames through Toast::present(it) instead of
// it->display(), which is what puts the toast on top of any screen.
//
// YAML usage:
//   Toast::setFonts(id(font_base), id(font_small));          // on boot
//   Toast::show("BATTERY 9%", "CHARGE SOON", 2000);          // then:
//   Toast::show("SENT", nullptr, 2000, Toast::ICON_CHECK_INLINE, 112, 30);
//   id(updated_ui) = true;                                   // draw it now
//   if (Toast::expired()) id(updated_ui) = true;             // each tick: redraw
//                                                            // once it's gone

class Toast {
public:
  // ICON_CHECK_INLINE: a small check right before the title, the two centred
  // together (confirmations like SENT); the others sit in a column on the left.
  enum Icon { ICON_NONE, ICON_CHECK, ICON_BATTERY, ICON_CHECK_INLINE };

  static void setFonts(esphome::font::Font* title, esphome::font::Font* body) {
    st().title_font = title;
    st().body_font  = body;
  }

  // width / height: box size in px (centred). height 0 = fit the text (30 with
  // a second line, 22 without); 30 gives a one-line toast the same box as a
  // two-line one.
  static void show(const char* title, const char* body = nullptr,
                   uint32_t duration_ms = 2000, Icon icon = ICON_NONE, int width = 112,
                   int height = 0) {
    State& s = st();
    copy(s.title, title);
    copy(s.body, body);
    s.icon  = icon;
    s.width  = (width < 48) ? 48 : (width > 120) ? 120 : width;
    s.height = (height < 0) ? 0 : (height > 40) ? 40 : height;
    s.until = esphome::millis() + duration_ms;
    if (s.until == 0) s.until = 1;  // 0 means "none"
    s.shown = true;
  }

  static bool active() {
    const State& s = st();
    return s.until != 0 && static_cast<int32_t>(esphome::millis() - s.until) < 0;
  }

  static void dismiss() { st().until = 0; }

  // True exactly once after the toast timed out or was dismissed, so the
  // caller can redraw the screen underneath without it.
  static bool expired() {
    State& s = st();
    if (!s.shown || active()) return false;
    s.shown = false;
    s.until = 0;
    return true;
  }

  // Draws the toast on top (while it's active) and sends the frame. Sends
  // nothing once stop() was called.
  template<class D>
  static void present(D* it) {
    if (st().stopped) return;
    if (active()) draw(it);
    it->display();
    frames()++;
  }

  // Frames sent so far, by any screen - lets a screen tell whether another
  // one was shown since its own last frame (Settings ABOUT replays its intro).
  static uint32_t& frames() { static uint32_t n = 0; return n; }

  // Deep-sleep shutdown (RemoteCore::halt): no screen may send another frame,
  // so a leftover refresh (Settings SYSTEM / ABOUT tick, a late state update)
  // can't draw over the power-off screen.
  static void stop() { st().stopped = true; }
  static bool stopped() { return st().stopped; }

private:
  struct State {
    char     title[24];
    char     body[24];
    Icon     icon;
    int      width;
    int      height;  // 0 = fit the text
    uint32_t until;   // millis() deadline, 0 = no toast
    bool     shown;   // set until expired() has reported the end
    bool     stopped; // stop(): present() sends nothing any more
    esphome::font::Font* title_font;
    esphome::font::Font* body_font;
  };

  static State& st() { static State s{}; return s; }

  static void copy(char* dst, const char* src) {
    if (!src) { dst[0] = '\0'; return; }
    strncpy(dst, src, 23);
    dst[23] = '\0';
  }

  template<class D>
  static void draw(D* it) {
    State& s = st();
    if (!s.title_font || !s.body_font) return;
    const bool has_body = s.body[0] != '\0';
    const int  w = s.width ? s.width : 112;
    const int  h = s.height ? s.height : (has_body ? 30 : 22);
    const int  x0 = (128 - w) / 2, y0 = 31 - h / 2;

    // clear margin, white box, black inner frame
    it->filled_rectangle(x0 - 2, y0 - 2, w + 4, h + 4, COLOR_OFF);
    it->filled_rectangle(x0, y0, w, h, COLOR_ON);
    it->rectangle(x0 + 2, y0 + 2, w - 4, h - 4, COLOR_OFF);

    int text_l = x0 + 4, text_r = x0 + w - 4;
    const int cy = y0 + h / 2;
    const bool inline_check = s.icon == ICON_CHECK_INLINE;
    if (s.icon != ICON_NONE && !inline_check) {
      drawIcon(it, s.icon, x0 + 7, cy);
      text_l = x0 + 7 + 16 + 3;
    }
    const int cx    = (text_l + text_r) / 2;
    const int avail = text_r - text_l;

    esphome::font::Font* tf = s.title_font;
    int bx, by, bw, bh;
    it->get_text_bounds(cx, 0, s.title, tf, esphome::display::TextAlign::CENTER, &bx, &by, &bw, &bh);
    if (bw > avail) tf = s.body_font;
    const int title_y = has_body ? y0 + 10 : cy;
    if (inline_check) {
      // 10x8 check, 4 px gap, title: centred as one group
      const int gx = cx - (10 + 4 + bw) / 2;
      for (int t = 0; t < 2; t++) {
        it->line(gx,     title_y + t,     gx + 3, title_y + 3 + t, COLOR_OFF);
        it->line(gx + 3, title_y + 3 + t, gx + 9, title_y - 4 + t, COLOR_OFF);
      }
      it->print(gx + 14, title_y, tf, COLOR_OFF, esphome::display::TextAlign::CENTER_LEFT, s.title);
    } else {
      it->print(cx, title_y, tf, COLOR_OFF, esphome::display::TextAlign::CENTER, s.title);
    }
    if (has_body)
      it->print(cx, y0 + 21, s.body_font, COLOR_OFF, esphome::display::TextAlign::CENTER, s.body);
  }

  // 16 px wide icons in black, vertically centred on cy.
  template<class D>
  static void drawIcon(D* it, Icon icon, int x, int cy) {
    if (icon == ICON_CHECK) {
      for (int t = 0; t < 2; t++) {
        it->line(x + 1, cy + t,     x + 5,  cy + 4 + t, COLOR_OFF);
        it->line(x + 5, cy + 4 + t, x + 14, cy - 5 + t, COLOR_OFF);
      }
    } else if (icon == ICON_BATTERY) {
      it->rectangle(x, cy - 4, 14, 9, COLOR_OFF);
      it->filled_rectangle(x + 14, cy - 2, 2, 5, COLOR_OFF);
      it->filled_rectangle(x + 2, cy - 2, 2, 5, COLOR_OFF);  // almost empty
    }
  }
};
