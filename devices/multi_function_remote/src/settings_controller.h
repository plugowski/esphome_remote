#pragma once
#include <string>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include "display_utils.h"

// ── Selectable row indices ────────────────────────────────────────────────────
static const int SETTING_CONTRAST      = 0;
static const int SETTING_BATT_INTERVAL = 1;   // battery check wake interval (minutes)
static const int SETTING_SLEEP         = 2;   // idle sleep timeout (minutes)
static const int SETTING_WIFI          = 3;
static const int SETTING_IP            = 4;
static const int SETTING_COUNT         = 5;

// ── SettingsController ────────────────────────────────────────────────────────
// Settings mode layout (128×64 OLED):
//
//   y= 0-12  │ [battery icon]              72%  │  fixed, never selected
//   y=13-25  │ visible slot 0                   │  selectable
//   y=26-38  │ visible slot 1                   │  selectable
//   y=39-51  │ visible slot 2                   │  selectable
//   y=53-63  │ ▲    │    ☰    │    ▼            │  bottom bar
//
// 5 settings rows scroll through 3 visible slots.
// contrast_level:        int 1–10  →  0.1–1.0 for set_contrast()
// sleep_timeout_mins:    int 1–30
// battery_interval_mins: int 15–1440, steps of 15

class SettingsController {
public:

  // ── navigation ───────────────────────────────────────────────────────────────

  static void prevSetting(int& idx) {
    idx = (idx - 1 + SETTING_COUNT) % SETTING_COUNT;
  }

  static void nextSetting(int& idx) {
    idx = (idx + 1) % SETTING_COUNT;
  }

  // ── contrast (1–10 steps) ────────────────────────────────────────────────────

  static void contrastUp(int& level, bool& updated_ui) {
    if (level < 10) { level++; updated_ui = true; }
  }

  static void contrastDown(int& level, bool& updated_ui) {
    if (level > 1) { level--; updated_ui = true; }
  }

  static float contrastFloat(int level) {
    return level * 0.1f;
  }

  // ── sleep timeout (1–30 minutes) ─────────────────────────────────────────────

  static void sleepUp(int& mins, bool& updated_ui) {
    if (mins < 30) { mins++; updated_ui = true; }
  }

  static void sleepDown(int& mins, bool& updated_ui) {
    if (mins > 1) { mins--; updated_ui = true; }
  }

  // ── battery check interval (15–1440 minutes, step 15) ────────────────────────

  static void battIntervalUp(int& mins, bool& updated_ui) {
    if (mins < 1440) { mins += 15; updated_ui = true; }
  }

  static void battIntervalDown(int& mins, bool& updated_ui) {
    if (mins > 15) { mins -= 15; updated_ui = true; }
  }

  // ── display ──────────────────────────────────────────────────────────────────

  template<class D, class F>
  static void draw(D* it, F* font_small,
                   int selected_idx, int contrast_level,
                   const std::string& ssid, const std::string& ip,
                   int battery_pct, int sleep_timeout_mins, int battery_interval_mins,
                   bool& updated_ui)
  {
    if (!updated_ui) return;
    updated_ui = false;
    it->clear();

    // ── Row 0: battery — fixed, not selectable ────────────────────────────────
    {
      const int y_c = 6;
      drawBattery(it, 4, y_c, battery_pct, COLOR_ON);
      char pct[6];
      if (battery_pct >= 0)
        snprintf(pct, sizeof(pct), "%d%%", battery_pct);
      else
        strncpy(pct, "---", sizeof(pct));
      it->print(124, y_c, font_small, COLOR_ON, display::TextAlign::CENTER_RIGHT, pct);
    }

    // ── Scrolling settings rows (3 visible out of SETTING_COUNT) ─────────────
    const int VISIBLE = 3;
    // Centre selected row in the visible window where possible
    int offset = std::max(0, std::min(selected_idx - 1, SETTING_COUNT - VISIBLE));

    static const char* labels[SETTING_COUNT] = {
      "CONTRAST", "BATTERY CHECK", "SLEEP", "WIFI", "IP"
    };

    for (int slot = 0; slot < VISIBLE; slot++) {
      int idx = offset + slot;
      if (idx >= SETTING_COUNT) break;

      const bool sel   = (idx == selected_idx);
      const int  y_top = (slot + 1) * 13;
      const int  y_c   = y_top + 6;

      Color fg = COLOR_ON;
      if (sel) {
        it->filled_rectangle(0, y_top, 128, 13, COLOR_ON);
        fg = COLOR_OFF;
      }

      it->print(4, y_c, font_small, fg, display::TextAlign::CENTER_LEFT, labels[idx]);

      switch (idx) {

        case SETTING_CONTRAST: {
          // 10 blocks × 3 px wide + 1 px gap, right-aligned to x=124
          for (int i = 0; i < 10; i++) {
            int bx = 85 + i * 4;
            if (i < contrast_level)
              it->filled_rectangle(bx, y_c - 3, 3, 6, fg);
            else
              it->rectangle(bx, y_c - 3, 3, 6, fg);
          }
          break;
        }

        case SETTING_WIFI: {
          char buf[20];
          strncpy(buf, ssid.c_str(), 19); buf[19] = '\0';
          it->print(124, y_c, font_small, fg, display::TextAlign::CENTER_RIGHT,
                    buf[0] ? buf : "---");
          break;
        }

        case SETTING_IP: {
          char buf[20];
          strncpy(buf, ip.c_str(), 19); buf[19] = '\0';
          it->print(124, y_c, font_small, fg, display::TextAlign::CENTER_RIGHT,
                    buf[0] ? buf : "---");
          break;
        }

        case SETTING_SLEEP: {
          char buf[10];
          formatMinutes(sleep_timeout_mins, buf, sizeof(buf));
          it->print(124, y_c, font_small, fg, display::TextAlign::CENTER_RIGHT, buf);
          break;
        }

        case SETTING_BATT_INTERVAL: {
          char buf[10];
          formatMinutes(battery_interval_mins, buf, sizeof(buf));
          it->print(124, y_c, font_small, fg, display::TextAlign::CENTER_RIGHT, buf);
          break;
        }
      }
    }

    draw_bottom_menu(it, font_small, "\u25b2", nullptr, "\u25bc");
    it->display();
  }

private:

  // Format minutes → "X MIN" (< 60), "X H" (whole hours), or "XH MM" (mixed)
  static void formatMinutes(int mins, char* buf, int buf_size) {
    if (mins < 60)
      snprintf(buf, buf_size, "%d MIN", mins);
    else if (mins % 60 == 0)
      snprintf(buf, buf_size, "%d H", mins / 60);
    else
      snprintf(buf, buf_size, "%dH%02d", mins / 60, mins % 60);
  }

  // Narrow battery icon: 16×6 body + 2×3 terminal nub on the right
  template<class D>
  static void drawBattery(D* it, int x, int y_c, int pct, Color fg) {
    it->rectangle(x, y_c - 3, 16, 6, fg);             // body  16×6
    it->filled_rectangle(x + 16, y_c - 1, 2, 3, fg);  // nub    2×3
    if (pct > 0) {
      int fill = (pct * 14) / 100;                     // max 14 px inside body
      it->filled_rectangle(x + 1, y_c - 2, fill, 4, fg);
    }
  }
};
