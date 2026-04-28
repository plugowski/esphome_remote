#pragma once

// ── RemoteCore ───────────────────────────────────────────────────────────────
// Shared boot/power-off screens and idle-timer helpers.
// All methods are static; call from YAML lambdas:
//   RemoteCore::drawBoot(id(oled_display), id(menu_icon_big), id(font_small));
//   RemoteCore::drawPowerOff(id(oled_display), id(font_base));
//   RemoteCore::resetIdleTimer(id(last_active));
//   RemoteCore::isIdle(id(last_active))

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

  // Power-off confirmation screen
  template<class D, class F>
  static void drawPowerOff(D* it, F* font_base) {
    it->clear();
    it->print(64, 32, font_base, COLOR_ON, display::TextAlign::CENTER, "POWER OFF");
    it->display();
  }

  // Connection error screen — status: 1=no wifi  2=no HA
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
    }
    it->clear();
    it->print(64,  8, font_large, COLOR_ON, display::TextAlign::CENTER, title);
    it->rectangle(0, 20, 128, 1);
    it->print(64, 36, font_small, COLOR_ON, display::TextAlign::CENTER, line1);
    it->print(64, 49, font_small, COLOR_ON, display::TextAlign::CENTER, line2);
    it->display();
  }

  // Reset the 120 s idle counter to now
  static void resetIdleTimer(int& last_active) {
    last_active = millis() / 1000;
  }

  // Returns true when the device has been idle for timeout_s seconds
  static bool isIdle(int last_active, int timeout_s = 120) {
    return (static_cast<int>(millis() / 1000) - last_active) >= timeout_s;
  }
};
