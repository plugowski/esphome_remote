#pragma once
#include "esp_sleep.h"
#include "esp_attr.h"
#include "esp_pm.h"

// Enables automatic FreeRTOS-tickless-idle light sleep for the current awake
// session (paired with CONFIG_PM_ENABLE / CONFIG_FREERTOS_USE_TICKLESS_IDLE
// in remote.yaml's sdkconfig_options — this call is a no-op without those).
// min/max_freq_mhz bounds dynamic frequency scaling; light_sleep_enable is
// the part that actually matters for current draw between button presses.
inline void configure_power_management() {
  esp_pm_config_t cfg = {};
  cfg.max_freq_mhz = 80;
  cfg.min_freq_mhz = 40;
  cfg.light_sleep_enable = true;
  esp_pm_configure(&cfg);
}

// Returns true when the device woke from the RTC timer (battery check wake).
// Returns false on first power-on or GPIO wakeup (user pressed a button).
inline bool is_timer_wakeup() {
  return esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER;
}

// ── RTC-memory state ─────────────────────────────────────────────────────────
// RTC_DATA_ATTR variables survive deep sleep (unlike normal RAM) but reset to
// their initializer on power-on / reflash — exactly the lifetime UI state like
// the active mode needs. Using RTC memory instead of ESPHome's flash-backed
// `restore_value: true` avoids a flash write on every mode change or button
// wake, which matters at button-press cadence over a multi-year device life.

RTC_DATA_ATTR int rtc_app_mode = 0;

inline void save_app_mode(int mode) { rtc_app_mode = mode; }
inline int load_app_mode() { return rtc_app_mode; }

// Set on every normal (button) wake; consumed by the next timer wake so a
// scheduled battery-report wake can skip its own WiFi/API round trip when a
// button wake already delivered a fresher reading to HA in the meantime.
RTC_DATA_ATTR bool rtc_recent_normal_wake = false;

inline void mark_normal_wake() { rtc_recent_normal_wake = true; }

inline bool consume_recent_normal_wake() {
  bool had = rtc_recent_normal_wake;
  rtc_recent_normal_wake = false;
  return had;
}
