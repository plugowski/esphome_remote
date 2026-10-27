#pragma once
#include <string>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include "display_utils.h"
#include "context_strip.h"
#include "remote_core.h"
#include "toast.h"
#include "system_stats.h"
#include "about_screen.h"
#include "battery_history.h"

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
static const int SETTING_SYSTEM        = 8;   // CPU / heap / battery / link bars (system_stats.h)
static const int SETTING_ABOUT         = 9;   // intro animation + credits (about_screen.h)
static const int SETTING_HISTORY       = 10;  // battery level over 24H / 7D / 30D (battery_history.h)
static const int SETTING_MAX           = 11;  // total rows that exist; not all are necessarily visible

// ── SettingsController ────────────────────────────────────────────────────────
// Settings mode layout (128×64 OLED) - one setting per screen:
//
//   rows 0..8   context strip: SETTINGS chip, one dot per setting, battery
//               with its % on the right
//   y=19        setting name (font_small), centred
//   rows 29..47 editable value in an input box: the value (font_base, or the
//               contrast level as 10 blocks of 5x10) centred on y=38, and a
//               ▲▼ section at the box's right edge - up / down change it
//               (+ / - on v1). Read-only values (WIFI / IP) are plain text.
//   y=53..63    < | ••• | >  bottom bar: round keys (and ← → on v2) =
//               previous / next setting
//
// The last pages are different: SYSTEM shows CPU / heap / battery / link
// bars (system_stats.h), HISTORY the battery level over the last day, week
// or month (battery_history.h; up / down switch the range) and ABOUT plays
// an intro animation into the credits (about_screen.h). All three redraw on
// a 50 ms tick while needsTick() says so.
//
// Up to 11 pages, one at a time (see visibleSettings()); which rows actually
// appear depends on battery_enabled / light_sleep / quiet_hours_on_device
// (all three are build-time substitutions passed in from mode_settings.yaml).
// contrast_level:           int 1–10  →  0.1–1.0 for set_contrast()
// sleep_timeout_mins:       int 1–60, on the SLEEP_LADDER steps
// battery_interval_mins:    int 15–20160 (14 days), a fixed ladder (hidden if !battery_enabled)
// deep_sleep_fallback_mins: int 0 (off) or 5-1440, steps of 5 (hidden unless light_sleep)
// quiet_hours_start/end:    int 0–23, wraps              (hidden unless light_sleep && quiet_hours_on_device)

class SettingsController {
public:

  // name and value box centred together as one group on the content rows 10..52
  static const int NAME_Y  = 19;  // setting name (font_small) centre row
  static const int VALUE_Y = 38;  // value / contrast blocks centre row
  static const int BOX_Y   = 29;  // input box around an editable value: rows 29..47
  static const int BOX_H   = 19;

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
    out[n++] = SETTING_SYSTEM;
    if (battery_enabled) out[n++] = SETTING_HISTORY;
    out[n++] = SETTING_ABOUT;
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

  // ── sleep timeout (1–60 minutes) ─────────────────────────────────────────────
  // 1-minute steps to 10, 5-minute steps to 30, then 45 and 60. A value off
  // the ladder (restored, or set by the generator) moves to the nearest one
  // in the pressed direction.

  static constexpr int SLEEP_LADDER[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 15, 20, 25, 30, 45, 60};
  static const int SLEEP_LADDER_LEN = sizeof(SLEEP_LADDER) / sizeof(SLEEP_LADDER[0]);

  static void sleepUp(int& mins, bool& updated_ui) {
    for (int i = 0; i < SLEEP_LADDER_LEN; i++) {
      if (SLEEP_LADDER[i] > mins) { mins = SLEEP_LADDER[i]; updated_ui = true; return; }
    }
  }

  static void sleepDown(int& mins, bool& updated_ui) {
    for (int i = SLEEP_LADDER_LEN - 1; i >= 0; i--) {
      if (SLEEP_LADDER[i] < mins) { mins = SLEEP_LADDER[i]; updated_ui = true; return; }
    }
  }

  // ── battery check interval (15 minutes to 14 days, on the same ladder as the generator slider) ──

  static void battIntervalUp(int& mins, bool& updated_ui) {
    static const int ladder[] = {15, 30, 60, 120, 180, 360, 720, 1440, 2880, 4320, 10080, 20160};
    for (int v : ladder) {
      if (v > mins) { mins = v; updated_ui = true; return; }
    }
  }

  static void battIntervalDown(int& mins, bool& updated_ui) {
    static const int ladder[] = {15, 30, 60, 120, 180, 360, 720, 1440, 2880, 4320, 10080, 20160};
    for (int i = 11; i >= 0; i--) {
      if (ladder[i] < mins) { mins = ladder[i]; updated_ui = true; return; }
    }
  }

  // Light Sleep only (0-1440 minutes, step 5). 0 disables the fallback
  // entirely - stays in Light Sleep indefinitely unless quiet hours apply.

  static void deepAfterUp(int& mins, bool& updated_ui) {
    if (mins < 1440) { mins += 5; updated_ui = true; }
  }

  static void deepAfterDown(int& mins, bool& updated_ui) {
    if (mins > 0) { mins -= 5; updated_ui = true; }
  }

  // ── quiet hours, Light Sleep only (0–23, wraps both ways) ────────────────────
  // Shared by both the start and end row — same wrap-around arithmetic either way.

  static void hourUp(int& hour, bool& updated_ui) {
    hour = (hour + 1) % 24; updated_ui = true;
  }

  static void hourDown(int& hour, bool& updated_ui) {
    hour = (hour + 23) % 24; updated_ui = true;
  }

  // ── battery history range (24H / 7D / 30D, wraps) ──────────────────────────
  // "Down" walks the list forward (24H → 7D → 30D), "up" back. The new range
  // is shown from its cache or fetched (BatteryHistory::open).

  static void historyRangeDown(int& range, bool& updated_ui) {
    BatteryHistory::rangeNext(range); BatteryHistory::open(range); updated_ui = true;
  }

  static void historyRangeUp(int& range, bool& updated_ui) {
    BatteryHistory::rangePrev(range); BatteryHistory::open(range); updated_ui = true;
  }

  // ── display ──────────────────────────────────────────────────────────────────

  template<class D, class F>
  static void draw(D* it, F* font_base, F* font_small,
                   int selected_idx, int contrast_level,
                   const std::string& ssid, const std::string& ip,
                   int battery_pct, int battery_mv, int sleep_timeout_mins, int battery_interval_mins,
                   int deep_sleep_fallback_mins, int quiet_hours_start, int quiet_hours_end,
                   bool battery_enabled, bool light_sleep, bool quiet_hours_on_device,
                   bool& updated_ui, int conn_status, int history_range,
                   const char* link_label = "WIFI", const char* addr_label = "IP")
  {
    if (!updated_ui) return;
    updated_ui = false;
    it->clear();

    int visible[SETTING_MAX];
    const int count = visibleSettings(visible, battery_enabled, light_sleep, quiet_hours_on_device);
    int pos = 0;
    for (int i = 0; i < count; i++) {
      if (visible[i] == selected_idx) { pos = i; break; }
    }
    const int idx = visible[pos];
    ContextStrip::setBattery(battery_pct);

    // A page "comes on screen" when it wasn't the last one drawn here, or
    // another screen sent a frame since (Toast::frames() moved on).
    const bool fresh = idx != lastIdx() || Toast::frames() != lastFrame();

    // ABOUT: the intro (re)starts each time the page comes on screen and
    // runs full screen; once done, the strip and keys go over its last frame
    if (idx == SETTING_ABOUT) {
      if (fresh) AboutScreen::start();
      AboutScreen::draw(it, font_base, font_small);
      AboutScreen::overlayChrome(it, font_small, pos, count, conn_status);
      present(it, idx);
      return;
    }

    // HISTORY: "BATTERY 24H" as the strip's badge, in place of the battery
    // icon and %; the page asks for its data when it comes on screen (open),
    // the tick sends the request
    if (idx == SETTING_HISTORY) {
      if (fresh) BatteryHistory::open(history_range);
      char badge[16];
      snprintf(badge, sizeof(badge), "BATTERY %s", BatteryHistory::rangeLabel(history_range));
      ContextStrip::draw(it, "SETTINGS", pos, count, conn_status, badge, false, 0, false);
      BatteryHistory::draw(it, history_range, battery_interval_mins);
      draw_bottom_menu(it, font_small, "<", nullptr, ">");
      present(it, idx);
      return;
    }

    // strip: SETTINGS chip, one dot per setting, battery with its %
    ContextStrip::draw(it, "SETTINGS", pos, count, conn_status, nullptr, true);

    if (idx == SETTING_SYSTEM) {
      if (fresh || SystemStats::due()) SystemStats::sample();
      SystemStats::draw(it, battery_pct, battery_mv);
      draw_bottom_menu(it, font_small, "<", nullptr, ">");
      present(it, idx);
      return;
    }

    static const char* labels[SETTING_MAX] = {
      "CONTRAST", "BATTERY CHECK", "SLEEP", "DEEP SLEEP", "QUIET FROM", "QUIET TO", "WIFI", "IP",
      "SYSTEM", "ABOUT", "HISTORY"
    };
    const char* name = idx == SETTING_WIFI ? link_label : idx == SETTING_IP ? addr_label : labels[idx];
    it->print(64, NAME_Y, font_small, COLOR_ON, display::TextAlign::CENTER, name);

    // value: contrast as 10 blocks, everything else as text in font_base.
    // An editable value sits in an input box with a ▲▼ section on its right
    // (up / down change it); a read-only one (WIFI / IP) is plain text,
    // font_small when it's too wide for font_base.
    const bool editable = idx != SETTING_WIFI && idx != SETTING_IP;
    char buf[20] = "";
    int vw = 59;  // contrast blocks: 10 x 5 px, 1 px apart
    F* vf = font_base;
    if (idx != SETTING_CONTRAST) {
      switch (idx) {
        case SETTING_BATT_INTERVAL: formatMinutes(battery_interval_mins, buf, sizeof(buf)); break;
        case SETTING_SLEEP:         formatMinutes(sleep_timeout_mins, buf, sizeof(buf)); break;
        case SETTING_DEEP_AFTER:    formatMinutes(deep_sleep_fallback_mins, buf, sizeof(buf)); break;
        case SETTING_QUIET_FROM:    formatHour(quiet_hours_start, buf, sizeof(buf)); break;
        case SETTING_QUIET_TO:      formatHour(quiet_hours_end, buf, sizeof(buf)); break;
        case SETTING_WIFI:          snprintf(buf, sizeof(buf), "%s", ssid.empty() ? "---" : ssid.c_str()); break;
        default:                    snprintf(buf, sizeof(buf), "%s", ip.empty() ? "---" : ip.c_str()); break;
      }
      int bx, by, bh;
      it->get_text_bounds(64, VALUE_Y, buf, font_base, display::TextAlign::CENTER, &bx, &by, &vw, &bh);
      if (!editable && vw > 120) vf = font_small;
    }

    int vx = 64;  // value centre
    if (editable) {
      // box: value area + 10 px ▲▼ section, at least 62 px wide, rows 29..47
      const int bw = std::max(vw, 40) + 22, x0 = 64 - bw / 2, sx = x0 + bw - 6;
      it->rectangle(x0, BOX_Y, bw, BOX_H, COLOR_ON);
      it->draw_pixel_at(x0,          BOX_Y,             COLOR_OFF);
      it->draw_pixel_at(x0 + bw - 1, BOX_Y,             COLOR_OFF);
      it->draw_pixel_at(x0,          BOX_Y + BOX_H - 1, COLOR_OFF);
      it->draw_pixel_at(x0 + bw - 1, BOX_Y + BOX_H - 1, COLOR_OFF);
      it->line(x0 + bw - 11, BOX_Y + 1, x0 + bw - 11, BOX_Y + BOX_H - 2, COLOR_ON);
      for (int k = 0; k < 3; k++) {
        it->line(sx - k, VALUE_Y - 5 + k, sx + k, VALUE_Y - 5 + k, COLOR_ON);  // ▲ apex at VALUE_Y-5
        it->line(sx - k, VALUE_Y + 5 - k, sx + k, VALUE_Y + 5 - k, COLOR_ON);  // ▼ apex at VALUE_Y+5
      }
      vx = x0 + (bw - 10) / 2;
    }

    if (idx == SETTING_CONTRAST) {
      const int x0 = vx - vw / 2;
      for (int i = 0; i < 10; i++) {
        const int bx = x0 + i * 6;
        if (i < contrast_level) it->filled_rectangle(bx, VALUE_Y - 5, 5, 10, COLOR_ON);
        else                    it->rectangle(bx, VALUE_Y - 5, 5, 10, COLOR_ON);
      }
    } else {
      it->print(vx, VALUE_Y, vf, COLOR_ON, display::TextAlign::CENTER, buf);
    }

    // round keys: previous / next setting
    draw_bottom_menu(it, font_small, "<", nullptr, ">");
    present(it, idx);
  }

  // The 50 ms tick (mode_settings.yaml) runs while this is true: the ABOUT
  // intro is playing or its strip and keys are sliding in (not in the pause
  // between), a SYSTEM sample is due, or HISTORY is waiting for its data.
  static bool needsTick(int selected_idx) {
    if (selected_idx == SETTING_ABOUT)   return AboutScreen::needsFrame();
    if (selected_idx == SETTING_SYSTEM)  return SystemStats::due();
    if (selected_idx == SETTING_HISTORY) return BatteryHistory::busy();
    return false;
  }

  // One tick: whether the page has to be redrawn now. ABOUT and SYSTEM
  // always (needsTick already said a frame is due); HISTORY only when its
  // loading animation moved on or the wait ended (BatteryHistory::poll).
  static bool tick(int selected_idx) {
    if (selected_idx == SETTING_HISTORY) return BatteryHistory::poll();
    return true;
  }

private:

  static int& lastIdx()        { static int v = -1; return v; }
  static uint32_t& lastFrame() { static uint32_t v = 0; return v; }

  // Sends the frame and remembers it as this page's, for the next `fresh`.
  template<class D>
  static void present(D* it, int idx) {
    Toast::present(it);
    lastIdx()   = idx;
    lastFrame() = Toast::frames();
  }

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

  // Format minutes -> "OFF" (0, only meaningful for DEEP_AFTER), "X MIN"
  // (< 60), "X H" (whole hours), or "XH MM" (mixed)
  static void formatMinutes(int mins, char* buf, int buf_size) {
    if (mins <= 0)
      snprintf(buf, buf_size, "OFF");
    else if (mins < 60)
      snprintf(buf, buf_size, "%d MIN", mins);
    else if (mins >= 1440 && mins % 1440 == 0)
      snprintf(buf, buf_size, "%d D", mins / 1440);
    else if (mins % 60 == 0)
      snprintf(buf, buf_size, "%d H", mins / 60);
    else
      snprintf(buf, buf_size, "%dH%02d", mins / 60, mins % 60);
  }
};
