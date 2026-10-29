#pragma once
#include <initializer_list>
#include "driver/rtc_io.h"
#include "esp_sleep.h"

// PCB v4.1+: every key wakes the ESP32-S3 through EXT1 (any key low). The
// keys have no external pull-ups, and ESPHome's deep_sleep sets up the EXT1
// mask without keeping the internal ones on, so the pins would float while
// asleep and wake the remote at random. Called on shutdown, right before
// deep sleep: keeps the RTC peripherals powered (a few uA) and the pull-ups
// enabled on the given pins.
inline void ext1_keep_pullups(std::initializer_list<int> pins) {
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
  for (int pin : pins) {
    rtc_gpio_pullup_en(static_cast<gpio_num_t>(pin));
    rtc_gpio_pulldown_dis(static_cast<gpio_num_t>(pin));
  }
}
