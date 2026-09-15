#pragma once
#include <string>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include "display_utils.h"
#include "remote_core.h"

// ── Selectable row identifiers ────────────────────────────────────────────────
// Fixed constants, not positions — a stored selected_setting_index always
// means the same row regardless of which ones a given build shows (see
// visibleSettings() below), so a value restored after reflashing with a
// feature toggled off still means what it always meant instead of pointing
// at a different row.
static const int SETTING_CONTRAST      = 0;
static const int SETTING_BATT_INTERVAL = 1;   // battery check wake interval (minutes)
static const int SETTING_SLEEP         = 2;   // idle sleep timeout (minutes)
static const int SETTING_DEEP_AFTER    = 3;   // Light Sleep only: total idle minutes -> real deep sleep
static const int SETTING_QUIET_FROM    = 4;   // Light Sleep + on-device only: quiet-hours start (0-23)
static const int SETTING_QUIET_TO      = 5;   // Light Sleep + on-device only: quiet-hours end (0-23)
static const int SETTING_WIFI          = 6;
static const int SETTING_IP            = 7;
static const int SETTING_MAX           = 8;   // total rows that exist; not all are necessarily visible

// ── SettingsController ────────────────────────────────────────────────────────
// Settings mode layout (128×64 OLED):
//
//   y= 0-12  │ [battery icon]              72%  │  fixed, never selected
//   y=13-25  │ visible slot 0                   │  selectable
//   y=26-38  │ visible slot 1                   │  selectable
//   y=39-51  │ visible slot 2                   │  selectable
//   y=53-63  │ ▲    │    ☰    │    ▼            │  bottom bar
//
// Up to 8 settings rows scroll through 3 visible slots; which rows actually
// appear depends on battery_enabled / light_sleep / quiet_hours_on_device
// (all three are build-time substitutions passed in from mode_settings.yaml).
// contrast_level:           int 1–10  →  0.1–1.0 for set_contrast()
// sleep_timeout_mins:       int 1–30
// battery_interval_mins:    int 15–1440, steps of 15   (hidden if !battery_enabled)
// deep_sleep_fallback_mins: int 5–1440, steps of 5      (hidden unless light_sleep)
// quiet_hours_start/end:    int 0–23, wraps              (hidden unless light_sleep && quiet_hours_on_device)

class SettingsController {
public:

  // ── visibility ────────────────────────────────────────────────────────────
  // Fills `out` with the rows this build actually shows, in display order,
  // and returns how many. DEEP_AFTER/QUIET_FROM/QUIET_TO are meaningless on
  // a Deep Sleep build (nothing ever reads them there), so they're hidden
  // together; QUIET_FROM/QUIET_TO are hidden further when the generator
  // already fixed the schedule at build time (quiet_hours_on_device false).
  static int visibleSettings(int* out, bool battery_enabled, bool light_sleep, bool quiet_hours_on_device) {
    int n = 0;
    out[n++] = SETTING_CONTRAST;
    if (battery_enabled) out[n++] = SETTING_BATT_INTERVAL;
    out[n++] = SETTING_SLEEP;
    if (light_sleep) {
      out[n++] = SETTING_DEEP_AFTER;
      if (quiet_hours_on_device) {
        out[n++] = SETTING_QUIET_FROM;
        out[n++] = SETTING_QUIET_TO;
      }
    }
    out[n++] = SETTING_WIFI;
    out[n++] = SETTING_IP;
    return n;
  }

  // ── navigation ───────────────────────────────────────────────────────────────

  static void prevSetting(int& idx, bool battery_enabled, bool light_sleep, bool quiet_hours_on_device) {
    move(idx, -1, battery_enabled, light_sleep, quiet_hours_on_device);
  }

  static void nextSetting(int& idx, bool battery_enabled, bool light_sleep, bool quiet_hours_on_device) {
    move(idx, +1, battery_enabled, light_sleep, quiet_hours_on_device);
  }

  // Snaps a restored index back onto this build's visible rows if it
  // landed on one that's hidden (e.g. reflashed with a feature turned off).
  static void sanitize(int& idx, bool battery_enabled, bool light_sleep, bool quiet_hours_on_device) {
    int visible[SETTING_MAX];
    int n = visibleSettings(visible, battery_enabled, light_sleep, quiet_hours_on_device);
    for (int i = 0; i < n; i++) {
      if (visible[i] == idx) return;
    }
    idx = visible[0];
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

  // ── deep-sleep fallback, Light Sleep only (5–1440 minutes, step 5) ───────────

  static void deepAfterUp(int& mins, bool& updated_ui) {
    if (mins < 1440) { mins += 5; updated_ui = true; }
  }

  static void deepAfterDown(int& mins, bool& updated_ui) {
    if (mins > 5) { mins -= 5; updated_ui = true; }
  }

  // ── quiet hours, Light Sleep only (0–23, wraps both ways) ────────────────────
  // Shared by both the start and end row — same wrap-around arithmetic either way.

  static void hourUp(int& hour, bool& updated_ui) {
    hour = (hour + 1) % 24; updated_ui = true;
  }

  static void hourDown(int& hour, bool& updated_ui) {
    hour = (hour + 23) % 24; updated_ui = true;
  }

  // ── display ──────────────────────────────────────────────────────────────────

  template<class D, class F>
  static void draw(D* it, F* font_small,
                   int selected_idx, int contrast_level,
                   const std::string& ssid, const std::string& ip,
                   int battery_pct, int sleep_timeout_mins, int battery_interval_mins,
                   int deep_sleep_fallback_mins, int quiet_hours_start, int quiet_hours_end,
                   bool battery_enabled, bool light_sleep, bool quiet_hours_on_device,
                   bool& updated_ui, int conn_status)
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

    // ── Scrolling settings rows (3 visible out of however many apply) ────────
    int visible[SETTING_MAX];
    const int count = visibleSettings(visible, battery_enabled, light_sleep, quiet_hours_on_device);

    int pos = 0;
    for (int i = 0; i < count; i++) {
      if (visible[i] == selected_idx) { pos = i; break; }
    }

    const int VISIBLE = 3;
    // Centre selected row in the visible window where possible
    int offset = std::max(0, std::min(pos - 1, count - VISIBLE));

    static const char* labels[SETTING_MAX] = {
      "CONTRAST", "BATTERY CHECK", "SLEEP", "DEEP SLEEP", "QUIET FROM", "QUIET TO", "WIFI", "IP"
    };

    for (int slot = 0; slot < VISIBLE; slot++) {
      int listPos = offset + slot;
      if (listPos >= count) break;
      int idx = visible[listPos];

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

        case SETTING_DEEP_AFTER: {
          char buf[10];
          formatMinutes(deep_sleep_fallback_mins, buf, sizeof(buf));
          it->print(124, y_c, font_small, fg, display::TextAlign::CENTER_RIGHT, buf);
          break;
        }

        case SETTING_QUIET_FROM: {
          char buf[8];
          formatHour(quiet_hours_start, buf, sizeof(buf));
          it->print(124, y_c, font_small, fg, display::TextAlign::CENTER_RIGHT, buf);
          break;
        }

        case SETTING_QUIET_TO: {
          char buf[8];
          formatHour(quiet_hours_end, buf, sizeof(buf));
          it->print(124, y_c, font_small, fg, display::TextAlign::CENTER_RIGHT, buf);
          break;
        }
      }
    }

    draw_bottom_menu(it, font_small, "▲", nullptr, "▼");
    RemoteCore::drawConnBadge(it, conn_status);
    it->display();
  }

private:

  static void move(int& idx, int step, bool battery_enabled, bool light_sleep, bool quiet_hours_on_device) {
    int visible[SETTING_MAX];
    int n = visibleSettings(visible, battery_enabled, light_sleep, quiet_hours_on_device);
    int pos = 0;
    for (int i = 0; i < n; i++) {
      if (visible[i] == idx) { pos = i; break; }
    }
    pos = wrap_index(pos, n, step);
    idx = visible[pos];
  }

  // Format an hour-of-day (0-23) as "HH:00". Equal start/end quiet-hours
  // values mean the schedule is disabled — shown as-is, no special case;
  // the "same time twice" is the disabled indicator.
  static void formatHour(int hour, char* buf, int buf_size) {
    snprintf(buf, buf_size, "%02d:00", ((hour % 24) + 24) % 24);
  }

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
    it->rectangle(x, y_c - 4, 14, 8, fg);             // body  16×8
    it->filled_rectangle(x + 14, y_c - 2, 2, 4, fg);  // nub    2×4
    if (pct > 0) {
      int fill = (pct * 12) / 100;                     // max 14 px inside body
      it->filled_rectangle(x + 1, y_c - 3, fill, 6, fg);
    }
  }
};
