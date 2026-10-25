#pragma once
#include <string>
#include <cstring>
#include <cctype>
#include <cstdio>
#include <cmath>
#include "light_entities.h"
#include "room_entities.h"
#include "display_utils.h"
#include "remote_core.h"
#include "context_strip.h"

// ── LightsController ──────────────────────────────────────────────────────────
// Lights mode. Two views, chosen at build time by the LIGHTS_CARD_VIEW
// substitution (global lights_card_view):
//
//   list view (draw)     — scrollable 4-row list, on/off toggle, ±5 % steps
//   card view (drawCard) — one light per screen: name row, big bulb icon,
//                          ON/OFF + brightness bar, LOW/FULL presets
//
// Button mapping, list view (original):
//   circles L/R (pin22/33) -> prev / next light   (▲ / ▼ in the bar)
//   ← →  (btn_left/right)  -> brightness − / +
//   OK                      -> toggle on/off
//   ↑ ↓                     -> prev / next light
//   ⏸                       -> (nothing)
//
// Button mapping, card view:
//   circles L/R (pin22/33) -> dimmable: LOW (25 %) / FULL (100 %)
//                             non-dimmable: OFF / ON
//   ← →  (btn_left/right)  -> prev / next light
//   OK                      -> ON   (keeps last brightness)
//   ⏸                       -> OFF
//   ↑ ↓                     -> brightness + / −
//   MENU (pin5)             -> opens mode menu  (both views)
//
// The routing itself lives in packages/mode_lights.yaml (lights_btn_*
// dispatcher scripts), so buttons.yaml / layout_v2.yaml never need to know
// which view is active.
//
// Both views start with the context strip (rows 0..8): "LIGHTS" chip, dots
// per page (list view: one per 4 lights, card view: one per light), a badge
// (list view: "ALL", there's no room filter yet; card view: the light's room,
// none without one), clock and battery. See src/context_strip.h.
//
// List view layout (128x64, 4 visible rows of 10 px from y=10, bottom menu
// at y=53; scrolls a whole page of 4 at a time so the dots match):
//   x=4            name (font_small, CENTER_LEFT — vertically centred in row)
//   x=after name   filled_circle r=2 (dimmable indicator) — placed 5 px after
//                  text right edge via get_text_bounds
//   x=110 right    brightness "75%" right-aligned (dimmable + ON, all rows)
//   x=119 r=3      radio button — filled=ON, outline=OFF
//   y=53..63       ▲ | ☰ | ▼  bottom bar (draw_bottom_menu)
//
// Card view layout:
//   y=9            name (font_base, TOP_LEFT at x=2, caps on rows 12..21)
//   x=2..34        26 px bulb \ue0f0 centred at (18, 36): filled glyph
//                  (lights_icon_on) when ON, outline (lights_icon_off) when OFF
//   x=40..126      dimmable: "ON"/"OFF" + "75%" (font_small, y=30) over a
//                  86x9 outline bar at y=38 (filled when ON, tick when OFF)
//                  non-dimmable: "ON"/"OFF" in font_base centred at (80, 36)
//   y=53..63       LOW | ☰ | FULL  (dimmable)   or   OFF | ☰ | ON
//
// YAML usage:
//   LightsController::prevLight(id(selected_light_index)); id(updated_ui)=true;
//   LightsController::nextLight(id(selected_light_index)); id(updated_ui)=true;
//   LightsController::toggleLight(id(selected_light_index), id(updated_ui));
//   LightsController::turnOn(id(selected_light_index), id(updated_ui));
//   LightsController::turnOff(id(selected_light_index), id(updated_ui));
//   LightsController::setPreset(id(selected_light_index), LightsController::PRESET_LOW, id(updated_ui));
//   LightsController::brightnessUp(id(selected_light_index), id(updated_ui));
//   LightsController::brightnessDown(id(selected_light_index), id(updated_ui));
//   LightsController::syncLight(idx, ha_state, ha_brightness, id(updated_ui));
//   LightsController::draw(id(oled_display), id(font_small),
//                          id(selected_light_index), id(updated_ui), id(conn_status));
//   LightsController::drawCard(id(oled_display), id(font_base), id(font_small),
//                              id(lights_icon_on), id(lights_icon_off),
//                              id(selected_light_index), id(updated_ui), id(conn_status));

class LightsController {
public:

  static const int ROWS   = 4;   // list view: rows per page
  static const int ROW_H  = 10;  // list view: row height below the context strip
  static const int NAME_Y = 9;   // card view: top of the name, under the strip

  // ── navigation ─────────────────────────────────────────────────────────────

  static void prevLight(int& idx) {
    idx = wrap_index(idx, LIGHTS_LIST_COUNT, -1);
  }

  static void nextLight(int& idx) {
    idx = wrap_index(idx, LIGHTS_LIST_COUNT, +1);
  }

  // ── toggle ─────────────────────────────────────────────────────────────────

  static void toggleLight(int idx, bool& updated_ui) {
    if (idx < 0 || idx >= LIGHTS_LIST_COUNT) return;
    lightOn(idx) = !lightOn(idx);
    updated_ui   = true;
  }

  static void turnOn(int idx, bool& updated_ui) {
    if (idx < 0 || idx >= LIGHTS_LIST_COUNT) return;
    lightOn(idx) = true;
    updated_ui   = true;
  }

  static void turnOff(int idx, bool& updated_ui) {
    if (idx < 0 || idx >= LIGHTS_LIST_COUNT) return;
    lightOn(idx) = false;
    updated_ui   = true;
  }

  // ── presets (card view circles) ───────────────────────────────────────────
  // Dimmable light: ON at the given percentage (0 % means OFF).
  // Non-dimmable light: the circles degrade to plain OFF / ON.
  static const int PRESET_LOW  = 25;
  static const int PRESET_FULL = 100;

  static void setPreset(int idx, int pct, bool& updated_ui) {
    if (idx < 0 || idx >= LIGHTS_LIST_COUNT) return;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    if (LIGHTS_LIST[idx].dimmable) {
      lightBrightness(idx) = pct;
      lightOn(idx)         = (pct > 0);
    } else {
      // No dimmer: the circles are plain OFF (low preset) / ON (full preset).
      lightOn(idx) = (pct >= PRESET_FULL);
    }
    updated_ui = true;
  }

  static bool isDimmable(int idx) {
    if (idx < 0 || idx >= LIGHTS_LIST_COUNT) return false;
    return LIGHTS_LIST[idx].dimmable;
  }

  static bool isOn(int idx) {
    if (idx < 0 || idx >= LIGHTS_LIST_COUNT) return false;
    return lightOn(idx);
  }

  // ── brightness (5 % steps, clamped 0-100, dimmable + ON only) ─────────────
  // HA reports brightness as a 0-255 raw value converted to a percentage, so
  // synced-in values land on odd numbers (21, 22, ...) more often than not.
  // Stepping by a flat +/-5 from there would just keep propagating that
  // oddness forever (22 -> 27 -> 32 -> ...). Snapping to the next clean
  // multiple of BRIGHTNESS_STEP in the direction of travel instead means
  // every value we ever send back to HA is already round, from the very
  // first press - not just once it happens to land on one by chance.
  static const int BRIGHTNESS_STEP = 5;

  static void brightnessUp(int idx, bool& updated_ui) {
    if (idx < 0 || idx >= LIGHTS_LIST_COUNT) return;
    if (!LIGHTS_LIST[idx].dimmable || !lightOn(idx)) return;
    int& b   = lightBrightness(idx);
    int next = (b / BRIGHTNESS_STEP + 1) * BRIGHTNESS_STEP;
    b        = (next <= 100) ? next : 100;
    updated_ui = true;
  }

  static void brightnessDown(int idx, bool& updated_ui) {
    if (idx < 0 || idx >= LIGHTS_LIST_COUNT) return;
    if (!LIGHTS_LIST[idx].dimmable || !lightOn(idx)) return;
    int& b   = lightBrightness(idx);
    int next = ((b - 1) / BRIGHTNESS_STEP) * BRIGHTNESS_STEP;
    b        = (next >= 0) ? next : 0;
    updated_ui = true;
  }

  // ── HA state sync ──────────────────────────────────────────────────────────

  // Looks up the light by entity_id so YAML lambdas never need a hardcoded index.
  static void syncLight(const char* entity_id, const std::string& ha_state,
                        float ha_brightness, bool& updated_ui) {
    int idx = indexForEntity(entity_id);
    if (idx < 0) return;
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
  // 4 rows of 10 px below the context strip; shows the page of 4 that holds
  // the selected light. Per row (height 10 px, font_small 10 px):
  //   selected row  → filled_rectangle background (inverted colours)
  //   name          → CENTER_LEFT at (4, y_center) — properly vertically centred
  //   level icon    → 3 growing bars, placed 4 px after right edge of name text
  //   brightness %  → right edge at x=110, all dimmable+ON rows
  //   radio button  → filled_circle (ON) / circle (OFF) at x=119, r=3
  template<class D, class F>
  static void draw(D* it, F* font_small, int selected_idx, bool& updated_ui, int conn_status) {
    if (!updated_ui) return;
    updated_ui = false;

    it->clear();

    // Page of 4 that holds the selected light
    const int page       = (selected_idx < 0) ? 0 : selected_idx / ROWS;
    const int page_count = (LIGHTS_LIST_COUNT + ROWS - 1) / ROWS;
    const int scroll_top = page * ROWS;
    ContextStrip::draw(it, "LIGHTS", page, page_count, conn_status, "ALL");

    for (int row = 0; row < ROWS; row++) {
      int li = scroll_top + row;
      if (li >= LIGHTS_LIST_COUNT) break;

      const bool sel  = (li == selected_idx);
      const bool on   = lightOn(li);
      const int  bri  = lightBrightness(li);
      const bool dim  = LIGHTS_LIST[li].dimmable;

      const int y_top    = ContextStrip::CONTENT_Y + row * ROW_H;
      const int y_center = y_top + 4;   // caps (7 px) on rows y_top+1..y_top+7

      Color fg = COLOR_ON;
      if (sel) {
        it->filled_rectangle(0, y_top, 128, ROW_H, COLOR_ON);
        fg = COLOR_OFF;
      }

      // Name — CENTER_LEFT anchors vertical centre to y_center.
      // Names may be up to 12 chars (generator limit); trim until the text
      // (plus the dimmable dot, 7 px) clears the brightness column that
      // starts at x=83 ("100%" right-aligned to 110) — 4 px gap.
      char buf[16];
      strncpy(buf, LIGHTS_LIST[li].name, sizeof(buf) - 1);
      buf[sizeof(buf) - 1] = '\0';
      for (int k = 0; buf[k]; k++) buf[k] = toupper((unsigned char)buf[k]);
      {
        const int name_max = (dim ? 83 - 7 : 83) - 4 - 4;
        int tx, ty, tw, th;
        it->get_text_bounds(4, y_center, buf, font_small,
                            display::TextAlign::CENTER_LEFT, &tx, &ty, &tw, &th);
        while (tw > name_max && strlen(buf) > 1) {
          buf[strlen(buf) - 1] = '\0';
          it->get_text_bounds(4, y_center, buf, font_small,
                              display::TextAlign::CENTER_LEFT, &tx, &ty, &tw, &th);
        }
      }
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
        it->filled_circle(119, y_center, 3, fg);
      } else {
        it->circle(119, y_center, 3, fg);
      }
    }

    draw_bottom_menu(it, font_small, "\u25b2", nullptr, "\u25bc");
    it->display();
  }

  // Renders the card view — one light per screen. No-ops when updated_ui is false.
  // icon_on:  26 px filled bulb (lightbulb \ue0f0, FILL=1 font) when ON
  // icon_off: 26 px outline bulb (lightbulb \ue0f0, regular font) when OFF
  template<class D, class F>
  static void drawCard(D* it, F* font_base, F* font_small, F* icon_on, F* icon_off,
                       int selected_idx, bool& updated_ui, int conn_status) {
    if (!updated_ui) return;
    updated_ui = false;

    it->clear();

    if (LIGHTS_LIST_COUNT <= 0) {
      ContextStrip::draw(it, "LIGHTS", 0, 0, conn_status, "ALL");
      it->print(64, 31, font_base, COLOR_ON, display::TextAlign::CENTER, "NO LIGHTS");
      draw_bottom_menu(it, font_small, "", nullptr, "");
      it->display();
      return;
    }

    const int  li  = wrap_index(selected_idx, LIGHTS_LIST_COUNT);
    const bool on  = lightOn(li);
    const int  bri = lightBrightness(li);
    const bool dim = LIGHTS_LIST[li].dimmable;

    // ── context strip: one dot per light, the light's room in the badge ──────
    ContextStrip::draw(it, "LIGHTS", li, LIGHTS_LIST_COUNT, conn_status,
                       roomName(LIGHTS_LIST[li].room));

    // ── name row ─────────────────────────────────────────────────────────────
    char buf[16];
    strncpy(buf, LIGHTS_LIST[li].name, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    for (int k = 0; buf[k]; k++) buf[k] = toupper((unsigned char)buf[k]);
    // Trim the name until it fits the screen width.
    const int name_max = 126 - 2;
    int tx, ty, tw, th;
    it->get_text_bounds(2, NAME_Y, buf, font_base, display::TextAlign::TOP_LEFT, &tx, &ty, &tw, &th);
    while (tw > name_max && strlen(buf) > 1) {
      buf[strlen(buf) - 1] = '\0';
      it->get_text_bounds(2, NAME_Y, buf, font_base, display::TextAlign::TOP_LEFT, &tx, &ty, &tw, &th);
    }
    it->print(2, NAME_Y, font_base, COLOR_ON, display::TextAlign::TOP_LEFT, buf);

    // ── icon: lit bulb when ON, plain bulb when OFF ──────────────────────────
    it->print(18, 36, on ? icon_on : icon_off, COLOR_ON, display::TextAlign::CENTER, "\ue0f0");

    if (dim) {
      // state + percentage on one line, brightness bar below
      it->print(40, 30, font_small, COLOR_ON, display::TextAlign::CENTER_LEFT, on ? "ON" : "OFF");
      char pbuf[6];
      snprintf(pbuf, sizeof(pbuf), "%d%%", bri);
      it->print(126, 30, font_small, COLOR_ON, display::TextAlign::CENTER_RIGHT, pbuf);

      it->rectangle(40, 38, 86, 9);                 // outline, inner span x=42..123 (82 px)
      const int w = (82 * ((bri < 0) ? 0 : (bri > 100) ? 100 : bri)) / 100;
      if (on) {
        if (w > 0) it->filled_rectangle(42, 40, w, 5);
      } else {
        // OFF: remembered level as a 2 px tick inside the empty bar
        int x = 42 + w;
        if (x > 123) x = 123;
        it->filled_rectangle(x, 38, 2, 9);
      }
      draw_bottom_menu(it, font_small, "LOW", nullptr, "FULL");
    } else {
      // non-dimmable: big state word centred in the free area right of the icon
      it->print(80, 36, font_base, COLOR_ON, display::TextAlign::CENTER, on ? "ON" : "OFF");
      draw_bottom_menu(it, font_small, "OFF", nullptr, "ON");
    }

    it->display();
  }

  // Public accessor for brightness — used by YAML lambda in lights_do_brightness
  static int getBrightness(int idx) {
    if (idx < 0 || idx >= LIGHTS_LIST_COUNT) return 0;
    return lightBrightness(idx);
  }

private:

  static int indexForEntity(const char* entity_id) {
    for (int i = 0; i < LIGHTS_LIST_COUNT; i++)
      if (strcmp(LIGHTS_LIST[i].entity_id, entity_id) == 0) return i;
    return -1;
  }

  // Per-light state stored in static locals (survives mode switches)
  static bool& lightOn(int idx) {
    static bool state[LIGHTS_LIST_CAPACITY] = {};
    return state[wrap_index(idx, LIGHTS_LIST_CAPACITY)];
  }

  static int& lightBrightness(int idx) {
    static int  bri[LIGHTS_LIST_CAPACITY];
    static bool init = false;
    if (!init) {
      init = true;
      for (int i = 0; i < LIGHTS_LIST_CAPACITY; i++) bri[i] = 50;
    }
    return bri[wrap_index(idx, LIGHTS_LIST_CAPACITY)];
  }
};
