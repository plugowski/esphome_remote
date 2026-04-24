#pragma once
#include <string>
#include <cstring>
#include <cctype>
#include <cstdio>
#include <cmath>
#include "light_entities.h"
#include "display_utils.h"

// ── LightsController ──────────────────────────────────────────────────────────
// Lights mode: scrollable list with on/off toggle, dimmable indicator,
// brightness control (10 % steps), and real-time HA state sync.
//
// Button mapping:
//   MODE  (pin22)   -> prevLight   (labeled ▲ on screen)
//   FAN   (pin33)   -> nextLight   (labeled ▼ on screen)
//   LEFT  (pin14)   -> nextLight   (mirrors FAN — scrolls down the list)
//   RIGHT (pin26)   -> toggleLight (on/off)
//   T_UP  (pin23/17)-> brightnessUp
//   T_DN  (pin19/18)-> brightnessDown
//   MENU  (pin5)    -> opens mode menu
//
// Display layout (128x64, 4 visible rows of 13 px + bottom menu at y=53):
//   x=4            name (font_small, CENTER_LEFT — vertically centred in row)
//   x=after name   filled_circle r=2 (dimmable indicator) — placed 5 px after
//                  text right edge via get_text_bounds
//   x=110 right    brightness "75%" right-aligned (dimmable + ON, all rows)
//   x=119 r=4      radio button — filled=ON, outline=OFF
//   y=53..63       ▲ | ☰ | ▼  bottom bar (draw_bottom_menu)
//
// YAML usage:
//   LightsController::prevLight(id(selected_light_index)); id(updated_ui)=true;
//   LightsController::nextLight(id(selected_light_index)); id(updated_ui)=true;
//   LightsController::toggleLight(id(selected_light_index), id(updated_ui));
//   LightsController::brightnessUp(id(selected_light_index), id(updated_ui));
//   LightsController::brightnessDown(id(selected_light_index), id(updated_ui));
//   LightsController::syncLight(idx, ha_state, ha_brightness, id(updated_ui));
//   LightsController::draw(id(oled_display), id(font_small),
//                          id(selected_light_index), id(updated_ui));

class LightsController {
public:

  // ── navigation ─────────────────────────────────────────────────────────────

  static void prevLight(int& idx) {
    idx = (idx - 1 + LIGHTS_LIST_COUNT) % LIGHTS_LIST_COUNT;
  }

  static void nextLight(int& idx) {
    idx = (idx + 1) % LIGHTS_LIST_COUNT;
  }

  // ── toggle ─────────────────────────────────────────────────────────────────

  static void toggleLight(int idx, bool& updated_ui) {
    if (idx < 0 || idx >= LIGHTS_LIST_COUNT) return;
    lightOn(idx) = !lightOn(idx);
    updated_ui   = true;
  }

  // ── brightness (10 % steps, clamped 0-100, dimmable + ON only) ────────────

  static void brightnessUp(int idx, bool& updated_ui) {
    if (idx < 0 || idx >= LIGHTS_LIST_COUNT) return;
    if (!LIGHTS_LIST[idx].dimmable || !lightOn(idx)) return;
    int& b     = lightBrightness(idx);
    b          = (b + 10 <= 100) ? b + 10 : 100;
    updated_ui = true;
  }

  static void brightnessDown(int idx, bool& updated_ui) {
    if (idx < 0 || idx >= LIGHTS_LIST_COUNT) return;
    if (!LIGHTS_LIST[idx].dimmable || !lightOn(idx)) return;
    int& b     = lightBrightness(idx);
    b          = (b - 10 >= 0) ? b - 10 : 0;
    updated_ui = true;
  }

  // ── HA state sync ──────────────────────────────────────────────────────────

  static void syncLight(int idx, const std::string& ha_state, float ha_brightness,
                        bool& updated_ui) {
    if (idx < 0 || idx >= LIGHTS_LIST_COUNT) return;
    bool changed = false;
    bool new_on  = (ha_state == "on");
    if (lightOn(idx) != new_on) { lightOn(idx) = new_on; changed = true; }
    if (!std::isnan(ha_brightness) && ha_brightness >= 0) {
      int b = static_cast<int>(ha_brightness);
      if (lightBrightness(idx) != b) { lightBrightness(idx) = b; changed = true; }
    }
    if (changed) updated_ui = true;
  }

  // ── display ────────────────────────────────────────────────────────────────

  // Renders the lights list.  No-ops when updated_ui is false.
  //
  // 4 rows of 13 px visible at a time; scrolls to keep the selected row visible.
  // Per row (height 13 px, font_small 10 px):
  //   selected row  → filled_rectangle background (inverted colours)
  //   name          → CENTER_LEFT at (4, y_center) — properly vertically centred
  //   level icon    → 3 growing bars, placed 4 px after right edge of name text
  //   brightness %  → right edge at x=110, all dimmable+ON rows
  //   radio button  → filled_circle (ON) / circle (OFF) at x=119, r=4
  template<class D, class F>
  static void draw(D* it, F* font_small, int selected_idx, bool& updated_ui) {
    if (!updated_ui) return;
    updated_ui = false;

    it->clear();

    // Adjust scroll viewport so the selected row is always visible
    static int scroll_top = 0;
    if (selected_idx < scroll_top)      scroll_top = selected_idx;
    if (selected_idx >= scroll_top + 4) scroll_top = selected_idx - 3;

    for (int row = 0; row < 4; row++) {
      int li = scroll_top + row;
      if (li >= LIGHTS_LIST_COUNT) break;

      const bool sel  = (li == selected_idx);
      const bool on   = lightOn(li);
      const int  bri  = lightBrightness(li);
      const bool dim  = LIGHTS_LIST[li].dimmable;

      const int y_top    = row * 13;
      const int y_center = y_top + 6;   // vertical centre of row

      Color fg = COLOR_ON;
      if (sel) {
        it->filled_rectangle(0, y_top, 128, 13, COLOR_ON);
        fg = COLOR_OFF;
      }

      // Name — CENTER_LEFT anchors vertical centre to y_center
      char buf[16];
      strncpy(buf, LIGHTS_LIST[li].name, sizeof(buf) - 1);
      buf[sizeof(buf) - 1] = '\0';
      for (int k = 0; buf[k]; k++) buf[k] = toupper((unsigned char)buf[k]);
      it->print(4, y_center, font_small, fg, display::TextAlign::CENTER_LEFT, buf);

      // Dimmable indicator — small filled circle (r=2) placed 5 px after name.
      if (dim) {
        int tx, ty, tw, th;
        it->get_text_bounds(4, y_center, buf, font_small,
                            display::TextAlign::CENTER_LEFT, &tx, &ty, &tw, &th);
        it->filled_circle(tx + tw + 7, y_center, 2, fg);
      }

      // Brightness % — right-aligned to x=110, shown for all dimmable+ON rows
      if (dim && on) {
        char pbuf[5];
        snprintf(pbuf, sizeof(pbuf), "%d%%", bri);
        it->print(110, y_center, font_small, fg, display::TextAlign::CENTER_RIGHT, pbuf);
      }

      // Radio button: filled circle = ON, outline circle = OFF
      if (on) {
        it->filled_circle(119, y_center, 4, fg);
      } else {
        it->circle(119, y_center, 4, fg);
      }
    }

    draw_bottom_menu(it, font_small, "\u25b2", "", "\u25bc");
    it->display();
  }

  // Public accessor for brightness — used by YAML lambda in lights_do_brightness
  static int getBrightness(int idx) {
    if (idx < 0 || idx >= LIGHTS_LIST_COUNT) return 0;
    return lightBrightness(idx);
  }

private:

  // Per-light state stored in static locals (survives mode switches)
  static bool& lightOn(int idx) {
    static bool state[LIGHTS_LIST_COUNT] = {};
    return state[idx];
  }

  static int& lightBrightness(int idx) {
    static int  bri[LIGHTS_LIST_COUNT];
    static bool init = false;
    if (!init) {
      init = true;
      for (int i = 0; i < LIGHTS_LIST_COUNT; i++) bri[i] = 50;
    }
    return bri[idx];
  }
};
