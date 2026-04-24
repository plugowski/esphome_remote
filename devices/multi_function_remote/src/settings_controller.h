#pragma once
#include <string>
#include <cstring>
#include <cstdio>
#include "display_utils.h"

// ── Selectable row indices (battery row is fixed at top, not selectable) ──────
static const int SETTING_CONTRAST = 0;
static const int SETTING_WIFI     = 1;
static const int SETTING_IP       = 2;
static const int SETTING_COUNT    = 3;   // only 3 rows are navigable

// ── SettingsController ────────────────────────────────────────────────────────
// Settings mode layout (128×64 OLED):
//
//   y= 0-12  │ [battery icon]              72%  │  fixed, never selected
//   y=13-25  │ CONTRAST  [■■■■■□□□□□]          │  selectable idx=0
//   y=26-38  │ WIFI      MyNetwork              │  selectable idx=1
//   y=39-51  │ IP        192.168.1.100          │  selectable idx=2
//   y=53-63  │ ▲    │         │    ▼            │  bottom bar
//
// contrast_level: int 1–10  →  0.1–1.0 for set_contrast()
// battery_pct:    int 0–100, or -1 when unknown

class SettingsController {
public:

  // ── navigation (skips the fixed battery row) ───────────────────────────────

  static void prevSetting(int& idx) {
    idx = (idx - 1 + SETTING_COUNT) % SETTING_COUNT;
  }

  static void nextSetting(int& idx) {
    idx = (idx + 1) % SETTING_COUNT;
  }

  // ── contrast (1–10 steps) ──────────────────────────────────────────────────

  static void contrastUp(int& level, bool& updated_ui) {
    if (level < 10) { level++; updated_ui = true; }
  }

  static void contrastDown(int& level, bool& updated_ui) {
    if (level > 1) { level--; updated_ui = true; }
  }

  static float contrastFloat(int level) {
    return level * 0.1f;
  }

  // ── display ────────────────────────────────────────────────────────────────

  template<class D, class F>
  static void draw(D* it, F* font_small,
                   int selected_idx, int contrast_level,
                   const std::string& ssid, const std::string& ip,
                   int battery_pct, bool& updated_ui)
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

    // ── Rows 1-3: selectable settings ────────────────────────────────────────
    static const char* labels[SETTING_COUNT] = { "CONTRAST", "WIFI", "IP" };

    for (int idx = 0; idx < SETTING_COUNT; idx++) {
      const bool sel   = (idx == selected_idx);
      const int  y_top = (idx + 1) * 13;   // offset by 1 for the battery row
      const int  y_c   = y_top + 6;

      Color fg = COLOR_ON;
      if (sel) {
        it->filled_rectangle(0, y_top, 128, 13, COLOR_ON);
        fg = COLOR_OFF;
      }

      it->print(4, y_c, font_small, fg, display::TextAlign::CENTER_LEFT, labels[idx]);

      switch (idx) {

        case SETTING_CONTRAST: {
          // 10 blocks, 3 px wide + 1 px gap = 4 px step, total 39 px
          // right-aligned to x=124: first block at x=85, last ends at x=124
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
          strncpy(buf, ssid.c_str(), 19);
          buf[19] = '\0';
          it->print(124, y_c, font_small, fg, display::TextAlign::CENTER_RIGHT,
                    buf[0] ? buf : "---");
          break;
        }

        case SETTING_IP: {
          char buf[20];
          strncpy(buf, ip.c_str(), 19);
          buf[19] = '\0';
          it->print(124, y_c, font_small, fg, display::TextAlign::CENTER_RIGHT,
                    buf[0] ? buf : "---");
          break;
        }
      }
    }

    draw_bottom_menu(it, font_small, "\u25b2", "", "\u25bc");
    it->display();
  }

private:

  // Narrow battery icon: 16×6 body + 2×3 terminal nub on the right
  template<class D>
  static void drawBattery(D* it, int x, int y_c, int pct, Color fg) {
    it->rectangle(x, y_c - 3, 16, 6, fg);             // body  16×6
    it->filled_rectangle(x + 16, y_c - 1, 2, 3, fg); // nub    2×3
    if (pct > 0) {
      int fill = (pct * 14) / 100;                    // max 14 px inside body
      it->filled_rectangle(x + 1, y_c - 2, fill, 4, fg);
    }
  }
};
