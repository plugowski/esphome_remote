#pragma once
#include "esp_sleep.h"
#include "esp_attr.h"

// Returns true when the device woke from the RTC timer (battery check wake).
// Returns false on first power-on or GPIO wakeup (user pressed a button).
inline bool is_timer_wakeup() {
  return esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER;
}

// ── RTC-memory state ─────────────────────────────────────────────────────────
// RTC_DATA_ATTR variables survive deep sleep (unlike normal RAM) but reset to
// their initializer on power-on / reflash. app_mode used to live here instead
// of ESPHome's flash-backed restore_value (to avoid a flash write on every
// mode change) - moved back to restore_value: true in remote.yaml because RTC
// memory doesn't survive an OTA reboot or power cycle, so a device that had
// just been reflashed (or lost power) always reopened on AC/mode 0 instead of
// wherever it was left - worse than the flash-wear tradeoff it was avoiding.

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
