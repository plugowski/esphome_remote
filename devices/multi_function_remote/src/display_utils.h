#pragma once
#include "esphome/components/display/display_buffer.h"
#include "esphome/components/font/font.h"

// Draws the bottom menu bar with labels for the three physical buttons:
//   left=pin22, center=pin5, right=pin33
//
// Center behaviour:
//   center == nullptr or ""  →  draws a ☰ burger icon (3 horizontal lines)
//   center has text          →  renders that text (e.g. "OK" in the menu screen)
//
// Pass nullptr or "" for left/right to hide those labels.
inline void draw_bottom_menu(
    esphome::display::DisplayBuffer *it,
    esphome::font::Font *font,
    const char *left,
    const char *center,
    const char *right
) {
    it->rectangle(0, 53, 128, 1);
    it->rectangle(46, 53, 36, 19);  // narrower centre box (36 px)

    if (left && left[0] != '\0') {
        it->print(23, 59, font, COLOR_ON, esphome::display::TextAlign::CENTER, left);
    }

    if (center && center[0] != '\0') {
        // Explicit label (e.g. "OK" in the mode-selection menu)
        it->print(64, 59, font, COLOR_ON, esphome::display::TextAlign::CENTER, center);
    } else {
        // Burger icon — 3 narrow horizontal lines centred in the box
        it->line(58, 56, 70, 56);
        it->line(58, 59, 70, 59);
        it->line(58, 62, 70, 62);
    }

    if (right && right[0] != '\0') {
        it->print(105, 59, font, COLOR_ON, esphome::display::TextAlign::CENTER, right);
    }
}

// Draws the top menu bar — same three buttons, mirrored to the top of the screen.
// Content below starts at y=11.
inline void draw_top_menu(
    esphome::display::DisplayBuffer *it,
    esphome::font::Font *font,
    const char *left,
    const char *center,
    const char *right
) {
    it->rectangle(0, 10, 128, 1);
    it->rectangle(42, 0, 44, 11);

    if (left && left[0] != '\0') {
        it->print(21, 5, font, COLOR_ON, esphome::display::TextAlign::CENTER, left);
    }
    if (center && center[0] != '\0') {
        it->print(64, 5, font, COLOR_ON, esphome::display::TextAlign::CENTER, center);
    }
    if (right && right[0] != '\0') {
        it->print(107, 5, font, COLOR_ON, esphome::display::TextAlign::CENTER, right);
    }
}
