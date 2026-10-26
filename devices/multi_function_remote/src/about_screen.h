#pragma once
#include <cmath>
#include <cstring>
#include "esphome/core/hal.h"
#include "about_face.h"
#include "context_strip.h"
#include "display_utils.h"

// ── AboutScreen ───────────────────────────────────────────────────────────────
// The Settings ABOUT page: a short hyperspace intro, then the credits.
//
//   0–80 ms      a few stars and a dot in the centre
//   80–900 ms    jump to light speed: streaks fly out of the centre while the
//                face grows from the dot to full height (64 px), speeding up
//   900–1100 ms  keeps growing to 104 px, rushing at the viewer
//   1100–1250 ms holds, oversized
//   1250–1650 ms shrinks back to 64 px while sliding to the left edge
//   1700–2397 ms the credits type in on the right, one letter every 17 ms
//   +3 s         the context strip eases in from the top and the < ••• >
//                keys from the bottom (300 ms), over the final frame, each
//                with 1 px of black around it - the frame itself doesn't move
//
// The animation runs full screen. SettingsController starts it whenever the
// ABOUT page comes on screen and redraws on a 50 ms tick while needsFrame():
// through the animation, not during the 3 s pause, then through the slide
// until one frame with the strip and keys fully in place has been drawn.
// The face comes as two bitmaps (about_face.h): 104 px tall for the
// oversized part, 64 px for its final size; anything in between or smaller
// is the nearest one at or above it, scaled down nearest-neighbour.

class AboutScreen {
public:
  static const uint32_t T_DOT      = 80;
  static const uint32_t T_ZOOM     = 900;
  static const uint32_t T_GROW     = 1100;
  static const uint32_t T_HOLD     = 1250;
  static const uint32_t T_SHRINK   = 1650;
  static const uint32_t T_TEXT     = 1700;
  static const uint32_t CHAR_MS    = 17;
  static const uint32_t CREDIT_CHARS = 11 + 5 + 8 + 17;     // all four lines
  static const uint32_t T_TYPED    = T_TEXT + CREDIT_CHARS * CHAR_MS;
  static const uint32_t T_CHROME   = T_TYPED + 3000;        // strip + keys start
  static const uint32_t CHROME_MS  = 300;                   // ... and take this

  static void start() {
    st().t0 = esphome::millis();
    st().started = true;
    st().final_drawn = false;
  }

  // Whether the 50 ms tick should redraw: anything still moving, nothing in
  // the pause before the strip and keys, and stop once they're fully in.
  static bool needsFrame() {
    if (!st().started || st().final_drawn) return false;
    const uint32_t t = elapsed();
    return t < T_TYPED + 2 * CHAR_MS || t >= T_CHROME;
  }

  // The animation frame for now (the caller adds overlayChrome() on top).
  template<class D, class F>
  static void draw(D* it, F* font_base, F* font_small) {
    const uint32_t t = elapsed();
    it->clear();

    if (t < T_DOT) {
      for (int i = 0; i < 10; i++) it->draw_pixel_at((i * 53 + 11) % 128, (i * 29 + 5) % 60 + 2, COLOR_ON);
      it->filled_rectangle(63, 30, 2, 2, COLOR_ON);
      return;
    }
    if (t < T_ZOOM) {                                   // ease-in zoom with streaks
      const float p = float(t - T_DOT) / (T_ZOOM - T_DOT);
      const float r0 = 2 + 60 * p * p;
      streaks(it, r0, r0 * 1.8f + 6);
      face(it, 64, 32, 2 + int(62 * p * p * p));
      return;
    }
    if (t < T_HOLD) {                                   // grow to 104 px, hold
      const float p = t < T_GROW ? easeOut(float(t - T_ZOOM) / (T_GROW - T_ZOOM)) : 1.0f;
      face(it, 64, 32 + int(2 * p), FACE_H + int((BIG_H - FACE_H) * p));
      return;
    }
    const float p = t < T_SHRINK ? easeInOut(float(t - T_HOLD) / (T_SHRINK - T_HOLD)) : 1.0f;
    face(it, 64 - int((64 - FINAL_CX) * p), 34 - int(2 * p),   // shrink + slide left
         BIG_H - int((BIG_H - FACE_H) * p));
    if (t >= T_TEXT) credits(it, font_base, font_small, int((t - T_TEXT) / CHAR_MS));
  }

  // The strip and the bottom keys over the final frame, once T_CHROME is
  // reached: they ease in from above / below the screen over CHROME_MS,
  // 1 px of black around each so they read cleanly on top of the face.
  template<class D, class F>
  static void overlayChrome(D* it, F* font_small, int pos, int count, int conn_status) {
    const uint32_t t = elapsed();
    if (t < T_CHROME) return;
    const float p = t < T_CHROME + CHROME_MS ? easeOut(float(t - T_CHROME) / CHROME_MS) : 1.0f;
    if (p >= 1.0f) st().final_drawn = true;

    const int sy = -int((ContextStrip::HEIGHT + 1) * (1 - p));          // -10 → 0
    it->filled_rectangle(0, sy, 128, ContextStrip::HEIGHT + 1, COLOR_OFF);
    ContextStrip::draw(it, "SETTINGS", pos, count, conn_status, nullptr, true, sy);

    const int by = SOFT_BTN_Y + int((64 - SOFT_BTN_Y + 1) * (1 - p));  // 65 → 53
    it->filled_rectangle(1, by - 1, 42, SOFT_BTN_H + 1, COLOR_OFF);
    it->filled_rectangle(46, by - 1, 36, SOFT_BTN_H + 1, COLOR_OFF);
    it->filled_rectangle(85, by - 1, 42, SOFT_BTN_H + 1, COLOR_OFF);
    draw_bottom_menu(it, font_small, "<", nullptr, ">", true, true, by);
  }

private:
  static const int BIG_H    = 104;  // oversized
  static const int FACE_H   = 64;   // final, full height: rows 0..63
  static const int FACE_W   = ABOUT_FACE_64_W;
  static const int FINAL_CX = FACE_W / 2;   // face against the left edge
  static const int TEXT_L   = FACE_W + 3;   // credits area: right of the face
                                            // to the screen edge

  struct State { uint32_t t0; bool started; bool final_drawn; };
  static State& st() { static State s{}; return s; }
  static uint32_t elapsed() { return esphome::millis() - st().t0; }

  static float easeOut(float p) { const float q = 1 - p; return 1 - q * q * q; }
  static float easeInOut(float p) {
    return p < 0.5f ? 4 * p * p * p : 1 - 4 * (1 - p) * (1 - p) * (1 - p);
  }

  // The face scaled to h px tall, centred on (cx, cy), clipped to the screen.
  template<class D>
  static void face(D* it, int cx, int cy, int h) {
    if (h < 2) { it->draw_pixel_at(cx, cy, COLOR_ON); return; }
    const bool big = h > FACE_H;
    const uint8_t* src = big ? ABOUT_FACE_104 : ABOUT_FACE_64;
    const int sw = big ? ABOUT_FACE_104_W : ABOUT_FACE_64_W;
    const int sh = big ? BIG_H : FACE_H;
    const int stride = big ? ABOUT_FACE_104_STRIDE : ABOUT_FACE_64_STRIDE;
    const int w = (sw * h + sh / 2) / sh;
    const int x0 = cx - w / 2, y0 = cy - h / 2;
    for (int ty = 0; ty < h; ty++) {
      const int y = y0 + ty;
      if (y < 0 || y > 63) continue;
      const uint8_t* row = src + (ty * sh / h) * stride;
      for (int tx = 0; tx < w; tx++) {
        const int x = x0 + tx;
        if (x < 0 || x > 127) continue;
        const int sx = tx * sw / w;
        if (row[sx >> 3] & (0x80 >> (sx & 7))) it->draw_pixel_at(x, y, COLOR_ON);
      }
    }
  }

  // 28 light-speed streaks from radius r0 to r1 (flattened to the 2:1 screen).
  template<class D>
  static void streaks(D* it, float r0, float r1) {
    for (int i = 0; i < 28; i++) {
      const float a = (i * 360.0f / 28 + (i % 2 ? 7 : 0)) * float(M_PI) / 180;
      const float k = 0.75f + 0.05f * ((i * 37) % 10);
      const float c = cosf(a) * k, s = sinf(a) * 0.6f * k;
      it->line(64 + int(c * r0), 31 + int(s * r0), 64 + int(c * r1), 31 + int(s * r1), COLOR_ON);
    }
  }

  // OLED REMOTE / PAWEL / LUGOWSKI / TECH.LUGOWSKI.DEV, typed in: only the
  // first `chars` letters of the block. Left-aligned as a block, centred in
  // the area right of the face, on rows 10..51 - clear of the overlay.
  template<class D, class F>
  static void credits(D* it, F* font_base, F* font_small, int chars) {
    static const char* const TITLE = "OLED REMOTE";
    static const char* const FIRST = "PAWEL";
    static const char* const LAST  = "LUGOWSKI";
    static const char* const URL   = "TECH.LUGOWSKI.DEV";
    int bx, by, bw, bh, widest = ContextStrip::textWidth(URL);
    it->get_text_bounds(0, 0, LAST, font_base, display::TextAlign::TOP_LEFT, &bx, &by, &bw, &bh);
    if (bw > widest) widest = bw;
    const int x = TEXT_L + (128 - TEXT_L - widest) / 2;

    char buf[20];
    const char* lines[4] = {TITLE, FIRST, LAST, URL};
    for (int i = 0; i < 4 && chars > 0; i++) {
      const int n = int(strlen(lines[i]));
      const int k = chars < n ? chars : n;
      memcpy(buf, lines[i], k);
      buf[k] = '\0';
      chars -= n;
      switch (i) {
        case 0: it->print(x, 14, font_small, COLOR_ON, display::TextAlign::CENTER_LEFT, buf); break;
        case 1: it->print(x, 26, font_base,  COLOR_ON, display::TextAlign::CENTER_LEFT, buf); break;
        case 2: it->print(x, 38, font_base,  COLOR_ON, display::TextAlign::CENTER_LEFT, buf); break;
        default: ContextStrip::text(it, x, 46, buf, COLOR_ON); break;
      }
    }
  }
};
