#pragma once
#include "toast.h"

// ── RemoteCore ───────────────────────────────────────────────────────────────
// Shared boot/power-off screens and idle-timer helpers.
// All methods are static; call from YAML lambdas:
//   RemoteCore::drawBoot(id(oled_display), id(menu_icon_big), id(font_small));
//   RemoteCore::drawPowerOff(id(oled_display), id(font_base));
//   RemoteCore::resetIdleTimer(id(last_active));
//   RemoteCore::isIdle(id(last_active))
//   RemoteCore::halt(id(oled_display));   // deep-sleep shutdown begins

class RemoteCore {
public:

  // Boot loading screen: hourglass icon + "LOADING"
  template<class D, class F>
  static void drawBoot(D* it, F* icon_big, F* font_small) {
    it->clear();
    it->print(64, 22, icon_big,   COLOR_ON, display::TextAlign::CENTER, "\ue88b");
    it->print(64, 50, font_small, COLOR_ON, display::TextAlign::CENTER, "LOADING");
    it->display();
  }

  // Deep-sleep shutdown begins: freezes the screen. No controller frame goes
  // out any more (Toast::present stops sending), the display's own 1 s poll
  // that pushes the framebuffer stops, and every key press is swallowed
  // (onUserActivity). Only the shutdown script's direct it->display() calls
  // (POWER OFF, then blank) still reach the panel.
  template<class D>
  static void halt(D* it) {
    Toast::stop();
    it->stop_poller();
  }

  static bool halted() { return Toast::stopped(); }

  // Power-off confirmation screen
  template<class D, class F>
  static void drawPowerOff(D* it, F* font_base) {
    it->clear();
    it->print(64, 32, font_base, COLOR_ON, display::TextAlign::CENTER, "POWER OFF");
    it->display();
  }

  // Connection error screen — status: 1=no wifi  2=no HA  3=no ESP-NOW hub
  template<class D, class F>
  static void drawConnError(D* it, F* font_large, F* font_small, int status) {
    const char* title = "";
    const char* line1 = "";
    const char* line2 = "";
    if (status == 1) {
      title = "NO WIFI";
      line1 = "Check network";
      line2 = "settings";
    } else if (status == 2) {
      title = "HA UNREACHABLE";
      line1 = "Check API key";
      line2 = "and permissions";
    } else if (status == 3) {
      title = "NO HUB";
      line1 = "Check hub power";
      line2 = "and channel";
    }
    it->clear();
    it->print(64,  8, font_large, COLOR_ON, display::TextAlign::CENTER, title);
    it->rectangle(0, 20, 128, 1);
    it->print(64, 36, font_small, COLOR_ON, display::TextAlign::CENTER, line1);
    it->print(64, 49, font_small, COLOR_ON, display::TextAlign::CENTER, line2);
    Toast::present(it);
  }

  // Reset the 120 s idle counter to now
  static void resetIdleTimer(int& last_active) {
    last_active = millis() / 1000;
  }

  // Call from every button handler instead of resetIdleTimer() directly, and
  // gate the handler's normal action on its (negated) return value — see
  // any binary_sensor in packages/layouts/. In Light Sleep builds the idle
  // timeout blanks the display but keeps WiFi/API associated rather than
  // rebooting from deep sleep; when a press lands on a blanked screen, this
  // wakes it (no reinit needed — the SSD1306/SH1106 controller keeps its
  // RAM/config while off, 0xAE) and reports "consumed" so the caller skips
  // its normal action this press — matching a phone screen, where the first
  // tap after screen-off only wakes it, nothing underneath it fires. A
  // no-op in Deep Sleep builds: is_light_sleeping is never true there, so
  // this always returns false and behaves exactly like resetIdleTimer.
  //
  // A press while a toast is showing closes it and is consumed the same way.
  //
  // conn_status: pass id(conn_status) from every key except Power Off. While
  // the NO HUB / NO WIFI / HA UNREACHABLE screen is up (non-zero) the press is
  // consumed, so nothing navigates or draws under it; it only asks for a
  // reconnect attempt right away (takeRetry). Once shutdown has begun
  // (halt) every press is consumed, Power Off included.
  template<class D>
  static bool onUserActivity(D* it, int& last_active, bool& is_light_sleeping, bool& updated_ui,
                             int conn_status = 0) {
    if (halted()) return true;
    resetIdleTimer(last_active);
    if (is_light_sleeping) {
      is_light_sleeping = false;
      it->turn_on();
      updated_ui = true;
      return true;
    }
    if (Toast::active()) {
      Toast::dismiss();
      updated_ui = true;
      return true;
    }
    if (conn_status != 0) {
      retryFlag() = true;
      return true;
    }
    return false;
  }

  // True once after a key was pressed on the connection-error screen.
  static bool takeRetry() {
    bool r = retryFlag();
    retryFlag() = false;
    return r;
  }

  // Returns true when the device has been idle for timeout_s seconds
  static bool isIdle(int last_active, int timeout_s = 120) {
    return (static_cast<int>(millis() / 1000) - last_active) >= timeout_s;
  }

  // Quiet hours for a Light Sleep build: true when hour (0-23) falls in
  // [start, end), wrapping past midnight when start > end (e.g. 19..6).
  // start == end means the schedule is disabled — always returns false.
  static bool inQuietHours(int hour, int start_hour, int end_hour) {
    if (start_hour == end_hour) return false;
    if (start_hour < end_hour) return hour >= start_hour && hour < end_hour;
    return hour >= start_hour || hour < end_hour;
  }

  // Connection-status glyph, drawn as a small overlay in the top-right corner
  // of whichever mode screen is already on-screen. Call from inside a mode's
  // draw(), right before it->display(), instead of blocking that mode's whole
  // screen on network state. status: 0=ok (draws nothing), 1=no wifi,
  // 2/3=wifi ok but HA/entities unreachable.
  template<class D>
  static void drawConnBadge(D* it, int status) {
    if (status == 0) return;
    if (status == 1) {
      it->rectangle(119, 1, 8, 8, COLOR_ON);
    } else {
      it->filled_rectangle(119, 1, 8, 8, COLOR_ON);
    }
  }

private:
  static bool& retryFlag() { static bool f = false; return f; }
};
