#pragma once
#include <cstdio>
#include "esphome/core/hal.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "context_strip.h"

// ── SystemStats ───────────────────────────────────────────────────────────────
// The Settings SYSTEM page: htop-style bars, sampled every 250 ms while the
// page is open.
//
//   rows 13..17   CPU  [||||||        ]  23%     busy share of both cores
//   rows 23..27   HEAP [||||||||||    ] 64/240K  internal RAM used / total
//   rows 33..37   BATT [||||||||||||  ]  3.92V   level bar, voltage (or %)
//   rows 43..47   LINK [||||||||||||| ]  -48DB   last packet from the hub
//                                                (ESP-NOW) or the router
//                                                (WiFi); bar -90..-30 dBm
//
// Labels and values in ContextStrip's 3x5 font; bars x=22..91, 5 px tall,
// filled with a |||| pattern (1 px gaps). CPU needs FreeRTOS run-time stats
// (CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS in the remote's sdkconfig): the
// idle tasks' share of the last 250 ms, per core, is what wasn't busy.
//
// YAML usage:
//   SystemStats::setLink(info.rx_ctrl->rssi);                 // ESP-NOW receive
//   SystemStats::setLink(wifi::global_wifi_component->wifi_rssi());  // WiFi
//   if (SystemStats::due()) SystemStats::sample();             // on the tick

class SystemStats {
public:
  static const uint32_t PERIOD_MS = 250;

  static bool due() { return esphome::millis() - st().last_ms >= PERIOD_MS; }

  static void sample() {
    State& s = st();
    s.last_ms = esphome::millis();
    s.heap_total = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
    s.heap_used  = s.heap_total - heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
#if (configGENERATE_RUN_TIME_STATS == 1) && !CONFIG_FREERTOS_SMP
    const int64_t now = esp_timer_get_time();
    uint32_t idle_sum = 0;
    for (int c = 0; c < portNUM_PROCESSORS; c++) {
      const uint32_t v = ulTaskGetIdleRunTimeCounterForCore(c);
      idle_sum += v - s.idle_prev[c];
      s.idle_prev[c] = v;
    }
    const int64_t span = (now - s.time_prev) * portNUM_PROCESSORS;
    if (s.time_prev != 0 && span > 0) {
      int busy = 100 - int(int64_t(idle_sum) * 100 / span);
      s.cpu = busy < 0 ? 0 : busy > 100 ? 100 : busy;
    }
    s.time_prev = now;
#endif
  }

  static void setLink(int dbm) { st().link = dbm; }

  template<class D>
  static void draw(D* it, int battery_pct, int battery_mv) {
    const State& s = st();
    char v[12];

    if (s.cpu >= 0) snprintf(v, sizeof(v), "%d%%", s.cpu); else snprintf(v, sizeof(v), "---");
    row(it, 0, "CPU", s.cpu >= 0 ? s.cpu / 100.0f : 0, v);

    snprintf(v, sizeof(v), "%u/%uK", unsigned(s.heap_used / 1024), unsigned(s.heap_total / 1024));
    row(it, 1, "HEAP", s.heap_total ? float(s.heap_used) / s.heap_total : 0, v);

    if (battery_mv > 0)       snprintf(v, sizeof(v), "%d.%02dV", battery_mv / 1000, (battery_mv % 1000) / 10);
    else if (battery_pct >= 0) snprintf(v, sizeof(v), "%d%%", battery_pct);
    else                       snprintf(v, sizeof(v), "---");
    row(it, 2, "BATT", battery_pct > 0 ? battery_pct / 100.0f : 0, v);

    const bool known = s.link > -127 && s.link < 0;
    if (known) snprintf(v, sizeof(v), "%dDB", s.link); else snprintf(v, sizeof(v), "---");
    float lf = known ? (s.link + 90) / 60.0f : 0;
    row(it, 3, "LINK", lf < 0 ? 0 : lf > 1 ? 1 : lf, v);
  }

private:
  struct State {
    uint32_t last_ms;
    size_t   heap_used, heap_total;
    int      cpu = -1;            // -1 until two samples exist
    int      link = -127;         // dBm, -127 = nothing received yet
    uint32_t idle_prev[2];
    int64_t  time_prev;
  };
  static State& st() { static State s{}; return s; }

  static const int BAR_X = 22, BAR_W = 70;

  template<class D>
  static void row(D* it, int k, const char* label, float frac, const char* value) {
    const int y = 13 + k * 10;
    ContextStrip::text(it, 2, y, label, COLOR_ON);
    it->rectangle(BAR_X, y, BAR_W, 5, COLOR_ON);
    const int fill = int((BAR_W - 2) * frac + 0.5f);
    if (fill > 0) it->filled_rectangle(BAR_X + 1, y + 1, fill, 3, COLOR_ON);
    for (int x = BAR_X + 2; x < BAR_X + 1 + fill; x += 2) it->draw_pixel_at(x, y + 2, COLOR_OFF);
    ContextStrip::text(it, 126 - ContextStrip::textWidth(value), y, value, COLOR_ON);
  }
};
