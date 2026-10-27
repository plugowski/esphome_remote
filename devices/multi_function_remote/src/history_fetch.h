#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "esphome/core/time.h"
#include "esphome/components/http_request/http_request.h"
#include "history_core.h"

// ── hist::fetch_ha ───────────────────────────────────────────────────────────
// Asks Home Assistant's history REST API for one entity and resamples the
// answer (hist::Resampler rules). Shared by the ESP-NOW hub and the WiFi
// remote, which fetches for itself; both need a long-lived access token
// (HA → profile → Security), the native API has no history call.
//
// Blocks the main loop for the duration of the request - well under a second
// on a LAN even for 30 days of hourly readings, since the answer is streamed
// through the scanner and never buffered whole.

namespace hist {

static const uint32_t FETCH_READ_TIMEOUT_MS = 5000;   // between two pieces of the answer

struct FetchResult {
  uint8_t  status{ST_HA_ERROR};
  uint32_t end_ts{0};        // UTC epoch, end of the last (running) bucket
  int32_t  tz_s{0};          // seconds to add to UTC for local time
  Resampler data;
};

// `now_utc` 0 (no valid clock) answers ST_NO_TIME; `url_base` without a
// trailing slash; `tag` names the caller in the log.
inline void fetch_ha(esphome::http_request::HttpRequestComponent* http, const std::string& url_base,
                     const std::string& token, const std::string& entity, uint32_t now_utc, int32_t tz_s,
                     uint16_t bucket_min, uint8_t count, FetchResult& out, const char* tag) {
  out.status = ST_HA_ERROR;
  if (http == nullptr || url_base.empty() || token.empty()) { out.status = ST_NOT_CONFIGURED; return; }
  if (now_utc == 0) { ESP_LOGW(tag, "History: no valid time yet"); out.status = ST_NO_TIME; return; }
  if (count == 0 || count > MAX_SAMPLES || bucket_min == 0) return;

  const uint32_t bucket_s = uint32_t(bucket_min) * 60;
  out.end_ts = bucket_end(now_utc, tz_s, bucket_s);
  out.tz_s   = tz_s;
  const uint32_t start = out.end_ts - bucket_s * count;

  char from[24], to[24];
  format_iso8601(start, from, sizeof(from));
  format_iso8601(out.end_ts, to, sizeof(to));
  // minimal_response: only state + last_changed per entry (the first and last keep their
  // attributes, which the scanner skips); no_attributes trims the rest.
  const std::string url = url_base + "/api/history/period/" + from + "?filter_entity_id=" + entity +
                          "&end_time=" + to + "&minimal_response&no_attributes";
  std::vector<esphome::http_request::Header> headers{{"Authorization", "Bearer " + token}};

  const uint32_t t0 = esphome::millis();
  auto container = http->get(url, headers);
  if (container == nullptr) {
    ESP_LOGW(tag, "History: Home Assistant not reachable at %s", url_base.c_str());
    return;
  }
  if (!esphome::http_request::is_success(container->status_code)) {
    ESP_LOGW(tag, "History: HTTP %d from Home Assistant (token valid? entity '%s' exists?)",
             container->status_code, entity.c_str());
    container->end();
    return;
  }

  out.data.begin(out.end_ts, bucket_s, count);
  HaHistoryScanner scanner(out.data);
  uint8_t buf[512];
  uint32_t last_data = esphome::millis();
  size_t total = 0;
  bool ok = true;
  while (true) {
    const int n = container->read(buf, sizeof(buf));
    esphome::App.feed_wdt();
    esphome::yield();
    const auto res = esphome::http_request::http_read_loop_result(n, last_data, FETCH_READ_TIMEOUT_MS,
                                                                  container->is_read_complete());
    if (res == esphome::http_request::HttpReadLoopResult::DATA) { scanner.feed(buf, size_t(n)); total += n; continue; }
    if (res == esphome::http_request::HttpReadLoopResult::RETRY) continue;
    if (res == esphome::http_request::HttpReadLoopResult::COMPLETE) break;
    ESP_LOGW(tag, "History: read %s after %u bytes",
             res == esphome::http_request::HttpReadLoopResult::TIMEOUT ? "timed out" : "failed", unsigned(total));
    ok = false;
    break;
  }
  container->end();
  if (!ok) return;
  out.data.finish();
  ESP_LOGI(tag, "History: %s, %u states in %u bytes, %u ms", entity.c_str(), unsigned(scanner.states()),
           unsigned(total), unsigned(esphome::millis() - t0));
  out.status = ST_OK;
}

}  // namespace hist
