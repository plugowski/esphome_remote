#pragma once
#include "esphome/components/display/display_buffer.h"
#include "esphome/components/font/font.h"

// ── Protected buffer access ───────────────────────────────────────────────────
// DisplayBuffer::buffer_ is protected; this accessor subclass exposes it.
// We never instantiate it — we only reinterpret_cast a live DisplayBuffer* to
// it so we can call raw_buffer(), which accesses buffer_ at the same offset.
struct DisplayBufferGrab : esphome::display::DisplayBuffer {
  const uint8_t* raw_buffer() const { return this->buffer_; }
};

// Retrieve the raw framebuffer from any DisplayBuffer-derived display object.
template<typename T>
inline const uint8_t* grab_raw_buffer(T* display) {
  return reinterpret_cast<DisplayBufferGrab*>(
    static_cast<esphome::display::DisplayBuffer*>(display)
  )->raw_buffer();
}

// Draws the bottom menu bar with labels for the three physical buttons:
//   left=pin22, center=pin5, right=pin33
//
// Special sentinel values (drawn as pixel icons, no font glyph needed):
//   center == nullptr or ""  →  ☰ hamburger (3 horizontal lines)
//   left   == "<"            →  ◀ left-pointing filled triangle
//   right  == ">"            →  ▶ right-pointing filled triangle
//   any other string         →  rendered as text
inline void draw_bottom_menu(
    esphome::display::DisplayBuffer *it,
    esphome::font::Font *font,
    const char *left,
    const char *center,
    const char *right
) {
    it->rectangle(0, 53, 128, 1);
    it->rectangle(46, 53, 36, 19);  // narrower centre box (36 px)

    if (left && left[0] == '<' && left[1] == '\0') {
        // ◀ rotated down-arrow; tip at left, base at right; 6×6 px centred at (20, 59)
        int cx = 20, cy = 59;
        it->line(cx,     cy,     cx,     cy + 1);  // col 0 — tip, 2 px
        it->line(cx + 1, cy,     cx + 1, cy + 1);  // col 1 — tip, 2 px
        it->line(cx + 2, cy - 1, cx + 2, cy + 2);  // col 2 — 4 px
        it->line(cx + 3, cy - 1, cx + 3, cy + 2);  // col 3 — 4 px
        it->line(cx + 4, cy - 2, cx + 4, cy + 3);  // col 4 — 6 px
        it->line(cx + 5, cy - 2, cx + 5, cy + 3);  // col 5 — base, 6 px
    } else if (left && left[0] != '\0') {
        it->print(23, 59, font, COLOR_ON, esphome::display::TextAlign::CENTER, left);
    }

    if (center && center[0] != '\0') {
        it->print(64, 59, font, COLOR_ON, esphome::display::TextAlign::CENTER, center);
    } else {
        // ☰ hamburger — 3 narrow horizontal lines
        it->line(58, 56, 70, 56);
        it->line(58, 59, 70, 59);
        it->line(58, 62, 70, 62);
    }

    if (right && right[0] == '>' && right[1] == '\0') {
        // ▶ pixel triangle, tip at right, centered at (105, 59)
        for (int i = 0; i < 5; i++)
            it->line(107 - i, 59 - i, 107 - i, 59 + i);
    } else if (right && right[0] != '\0') {
        it->print(105, 59, font, COLOR_ON, esphome::display::TextAlign::CENTER, right);
    }
}
