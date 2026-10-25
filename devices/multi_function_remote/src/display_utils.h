#pragma once
#include "esphome/components/display/display_buffer.h"
#include "esphome/components/font/font.h"
#include "toast.h"

// ── Safe index wrapping ───────────────────────────────────────────────────────
// Every list in this project (modes, entities, quick actions) is generated from
// the configurator, so any of them can legitimately end up with a count of 0.
// Writing `idx = (idx + 1) % COUNT` against a constant 0 is a compile-time
// division by zero (-Wdiv-by-zero) and undefined behaviour at runtime — on the
// ESP32 that is a hard fault, not a wrong number. Routing the count through a
// function parameter both silences the constant-folding warning and makes the
// zero case well defined, so controllers must wrap indices with this helper
// instead of using % directly.
inline int wrap_index(int idx, int count, int step = 0) {
  if (count <= 0) return 0;
  long long next = static_cast<long long>(idx) + step;
  next %= count;
  if (next < 0) next += count;
  return static_cast<int>(next);
}

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

// ── Bottom bar ────────────────────────────────────────────────────────────────
// Soft buttons over the three physical keys under the screen:
//   left=pin22, center=pin5, right=pin33
//
// Each label sits on an inverted badge with its corner pixels cut, rows
// 53..63, content in black:
//   left   x=2..41    (40 px)
//   centre x=47..80   (34 px, narrower: its labels are a glyph or "OK";
//                     frame only by default, see SOFT_BTN_CENTER_OUTLINE)
//   right  x=86..125  (40 px)
//
// Labels, unchanged from the old frame-style bar:
//   left / right == nullptr or ""  →  no button on that side
//   center == nullptr              →  ☰ hamburger (3 horizontal lines)
//   center == ""                   →  no centre button
//   left   == "<"                  →  ◀ left-pointing filled triangle
//   right  == ">"                  →  ▶ right-pointing filled triangle
//   any other string               →  rendered as text
//
// SOFT_BTN_CENTER_OUTLINE (below): 1 = the centre button is only a frame
// with white content (sets the menu / OK key apart from the per-screen
// actions on the sides), 0 = filled like the sides.
//
// center_box: true (default) draws the centre as a button like the sides.
//             false prints the centre label as plain text with no button, for
//             wider notes (the welcome firmware's "POWER = exit").
// separator:  only with center_box=false: the old line at y=53 above it.
#ifndef SOFT_BTN_CENTER_OUTLINE
#define SOFT_BTN_CENTER_OUTLINE 1   // override with -DSOFT_BTN_CENTER_OUTLINE=0
#endif

static const int SOFT_BTN_Y = 53, SOFT_BTN_H = 11;

// One button; outline = frame only (content then goes in COLOR_ON).
inline void draw_soft_button(esphome::display::Display *it, int x, int w, bool outline = false) {
    if (outline) it->rectangle(x, SOFT_BTN_Y, w, SOFT_BTN_H, COLOR_ON);
    else         it->filled_rectangle(x, SOFT_BTN_Y, w, SOFT_BTN_H, COLOR_ON);
    const int y2 = SOFT_BTN_Y + SOFT_BTN_H - 1;
    it->draw_pixel_at(x,         SOFT_BTN_Y, COLOR_OFF);
    it->draw_pixel_at(x + w - 1, SOFT_BTN_Y, COLOR_OFF);
    it->draw_pixel_at(x,         y2,         COLOR_OFF);
    it->draw_pixel_at(x + w - 1, y2,         COLOR_OFF);
}

inline void draw_bottom_menu(
    esphome::display::Display *it,
    esphome::font::Font *font,
    const char *left,
    const char *center,
    const char *right,
    bool center_box = true,
    bool separator = true
) {
    using esphome::display::TextAlign;
    const int cy = SOFT_BTN_Y + 5;  // vertical centre of the buttons (row 58)

    if (left && left[0] != '\0') {
        draw_soft_button(it, 2, 40);
        if (left[0] == '<' && left[1] == '\0') {
            // ◀ 6x6, tip at left, centred on the button (x=19..24)
            const int x = 19;
            it->line(x,     cy,     x,     cy + 1, COLOR_OFF);
            it->line(x + 1, cy,     x + 1, cy + 1, COLOR_OFF);
            it->line(x + 2, cy - 1, x + 2, cy + 2, COLOR_OFF);
            it->line(x + 3, cy - 1, x + 3, cy + 2, COLOR_OFF);
            it->line(x + 4, cy - 2, x + 4, cy + 3, COLOR_OFF);
            it->line(x + 5, cy - 2, x + 5, cy + 3, COLOR_OFF);
        } else {
            it->print(22, cy, font, COLOR_OFF, TextAlign::CENTER, left);
        }
    }

    if (center_box) {
        const bool  outline = SOFT_BTN_CENTER_OUTLINE;
        const Color ink     = outline ? COLOR_ON : COLOR_OFF;
        if (!center) {
            // nullptr → ☰ hamburger
            draw_soft_button(it, 47, 34, outline);
            it->line(58, cy - 3, 70, cy - 3, ink);
            it->line(58, cy,     70, cy,     ink);
            it->line(58, cy + 3, 70, cy + 3, ink);
        } else if (center[0] != '\0') {
            draw_soft_button(it, 47, 34, outline);
            it->print(64, cy, font, ink, TextAlign::CENTER, center);
        }
    } else {
        // plain centre text, no button (wide notes)
        if (separator) it->rectangle(0, 53, 128, 1);
        if (center && center[0] != '\0')
            it->print(64, 59, font, COLOR_ON, TextAlign::CENTER, center);
    }

    if (right && right[0] != '\0') {
        draw_soft_button(it, 86, 40);
        if (right[0] == '>' && right[1] == '\0') {
            // ▶ 6x6, tip at right, centred on the button (x=103..108)
            const int x = 103;
            it->line(x,     cy - 2, x,     cy + 3, COLOR_OFF);
            it->line(x + 1, cy - 2, x + 1, cy + 3, COLOR_OFF);
            it->line(x + 2, cy - 1, x + 2, cy + 2, COLOR_OFF);
            it->line(x + 3, cy - 1, x + 3, cy + 2, COLOR_OFF);
            it->line(x + 4, cy,     x + 4, cy + 1, COLOR_OFF);
            it->line(x + 5, cy,     x + 5, cy + 1, COLOR_OFF);
        } else {
            it->print(106, cy, font, COLOR_OFF, TextAlign::CENTER, right);
        }
    }
}

// Left / right soft buttons only (no centre), e.g. the quick-action view's
// BACK / RUN.
inline void draw_soft_buttons(
    esphome::display::Display *it,
    esphome::font::Font *font,
    const char *left,
    const char *right
) {
    draw_bottom_menu(it, font, left, "", right);
}
