#pragma once
#include "esp_sleep.h"

// Returns true when the device woke from the RTC timer (battery check wake).
// Returns false on first power-on or GPIO wakeup (user pressed a button).
inline bool is_timer_wakeup() {
  return esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER;
}
