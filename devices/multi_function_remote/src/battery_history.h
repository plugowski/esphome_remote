#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "esphome/core/hal.h"
#include "display_utils.h"
#include "context_strip.h"
#include "history_core.h"
#include "espnow_protocol.h"

// ── BatteryHistory ───────────────────────────────────────────────────────────
// The Settings HISTORY page: the battery level over the last 24 hours, 7 or
// 30 days, like a phone's battery graph. The remote keeps no history of its
// own - the samples come from Home Assistant's Recorder (through the hub on
// the ESP-NOW remote, straight from the history REST API on the WiFi one),
// already resampled into hist::RANGES buckets, and stay cached in RAM for
// CACHE_MS so flipping between ranges doesn't ask again.
//
//   rows 0..8     context strip: SETTINGS chip, dots, the range as badge
//                 (24H / 7D / 30D), battery with its %
//   rows 14..43   plot, columns 2..111 (110 px): dotted lines at 100 % and
//                 50 %, solid baseline at 0 %, the level as 29 px of height.
//                 100% / 50% / 0% labels sit right of the plot, right-aligned
//                 to the edge of the screen.
//                   24H  one bar per hour (3-4 px, 1 px gap); a hollow bar =
//                        the level went up during that hour (charging)
//                   7D / 30D  one column per 2 h / 6 h, dithered area with a
//                        solid top edge; a charge shows as a vertical jump
//                 A sample Home Assistant has no value for is left out: a gap,
//                 never drawn as 0 %.
//   rows 46..50   time axis (3x5 font): 24H hours "00 06 12 18", 7D a letter
//                 per day (today inverted), 30D week ticks with a D.M date.
//                 Without a clock (end_ts 0) the labels count back instead:
//                 -18H, -6D, -3W. A 2 px tick under the newest column = now.
//   rows 53..63   < ••• > soft keys (previous / next setting, as everywhere
//                 in Settings); up / down switch the range
//
// Loading / failure / no-data states print a line or two in the 3x5 font
// over the empty plot; LOADING animates its dots on the 50 ms Settings tick.
//
// Flow (YAML): open(range) when the page comes on screen or the range
// changes → the tick calls poll() and takeSendRequest() → the request goes
// out → onPacket() (ESP-NOW, one or more HIST_DATA fragments) or setResult()
// (WiFi) fills the range's series → READY. No answer within REPLY_TIMEOUT_MS
// asks once more, then FAILED.

class BatteryHistory {
public:
  enum State : uint8_t { IDLE, LOADING, READY, FAILED };
  enum Fail  : uint8_t { F_NONE, F_SEND, F_NO_REPLY, F_HA, F_NO_TIME, F_NOT_CONFIGURED };

  static const uint32_t CACHE_MS         = 5 * 60 * 1000;
  static const uint32_t REPLY_TIMEOUT_MS = 4000;
  static const int      MAX_TRIES        = 2;

  // plot geometry
  static const int X0 = 2, W = 110;          // plot columns 2..111; the % labels take 113..127
  static const int Y100 = 14, Y50 = 28, Y0 = 43;
  static const int PLOT_H = Y0 - Y100;       // 29 px for 0..100 %
  static const int AXIS_Y = 46;              // time labels (rows 46..50)

  // ── range ─────────────────────────────────────────────────────────────────

  static int  clampRange(int r) { return (r < 0 || r >= hist::RANGE_COUNT) ? 0 : r; }
  static void rangeNext(int& r) { r = (clampRange(r) + 1) % hist::RANGE_COUNT; }
  static void rangePrev(int& r) { r = (clampRange(r) + hist::RANGE_COUNT - 1) % hist::RANGE_COUNT; }
  static const char* rangeLabel(int r) { return hist::RANGES[clampRange(r)].label; }

  // ── lifecycle ─────────────────────────────────────────────────────────────

  // The page comes on screen, or the range changed: show the cached series
  // while it's fresh, otherwise ask for it.
  static void open(int range) {
    St& s = st();
    s.range = clampRange(range);
    const Series& c = s.series[s.range];
    if (c.ready && !olderThan(c.fetched_ms, CACHE_MS)) { s.state = READY; return; }
    s.state = LOADING;
    s.fail = F_NONE;
    s.tries = 0;
    s.want_send = true;
    s.anim = 0;
  }

  // Polled from the Settings tick: true once for every request that has to go
  // out (the caller then sends it with these parameters).
  static bool takeSendRequest(int& range, uint8_t& req_id, uint16_t& bucket_min, uint8_t& count) {
    St& s = st();
    if (!s.want_send) return false;
    s.want_send = false;
    s.tries++;
    s.req_id++;
    s.sent_ms = esphome::millis();
    s.frag_mask = 0;
    range      = s.range;
    req_id     = s.req_id;
    bucket_min = hist::RANGES[s.range].bucket_min;
    count      = hist::RANGES[s.range].count;
    return true;
  }

  // The request could not be delivered (no hub acknowledging the packet).
  static void sendFailed() { fail(F_SEND); }

  // ESP-NOW: one HIST_DATA fragment (the reader stands after the header).
  // Fragments of an older request, or of another range, are ignored.
  static void onPacket(rnp::Reader& r) {
    St& s = st();
    uint8_t req_id, status, count, frag, frag_count, offset, n;
    uint16_t bucket_min;
    uint32_t end_ts;
    int16_t tz_min;
    if (!r.u8(req_id) || !r.u8(status) || !r.u16(bucket_min) || !r.u8(count) || !r.u32(end_ts) || !r.i16(tz_min) ||
        !r.u8(frag) || !r.u8(frag_count) || !r.u8(offset) || !r.u8(n))
      return;
    if (s.state != LOADING || req_id != s.req_id) return;
    if (status != hist::ST_OK) { fail(fromStatus(status)); return; }
    if (count == 0 || count > hist::MAX_SAMPLES || frag_count == 0 || frag_count > 8 || frag >= frag_count ||
        offset + n > count)
      return;
    Series& c = s.series[s.range];
    if (s.frag_mask == 0) {                      // first fragment of this answer
      c.count = count; c.bucket_min = bucket_min; c.end_ts = end_ts; c.tz_s = int32_t(tz_min) * 60;
      memset(c.values, 0, sizeof(c.values));
      memset(c.valid, 0, sizeof(c.valid));
    }
    uint8_t bits[(rnp::HIST_FRAG_SAMPLES + 7) / 8];
    for (size_t b = 0; b < (size_t(n) + 7) / 8; b++) if (!r.u8(bits[b])) return;
    for (size_t k = 0; k < n; k++) {
      uint8_t v;
      if (!r.u8(v)) return;
      const size_t i = offset + k;
      c.values[i] = v > 100 ? 100 : v;
      if (bits[k / 8] & (1 << (k % 8))) c.valid[i / 8] |= 1 << (i % 8);
    }
    s.frag_mask |= 1 << frag;
    if (s.frag_mask == (1u << frag_count) - 1) ready(c);
  }

  // WiFi remote: the whole answer at once (values / valid as in hist::Resampler).
  static void setResult(int range, uint8_t status, uint16_t bucket_min, uint8_t count, uint32_t end_ts, int32_t tz_s,
                        const uint8_t* values, const uint8_t* valid) {
    St& s = st();
    range = clampRange(range);
    if (status != hist::ST_OK) { if (s.range == range) fail(fromStatus(status)); return; }
    Series& c = s.series[range];
    c.count = count > hist::MAX_SAMPLES ? hist::MAX_SAMPLES : count;
    c.bucket_min = bucket_min; c.end_ts = end_ts; c.tz_s = tz_s;
    memcpy(c.values, values, c.count);
    memcpy(c.valid, valid, (c.count + 7) / 8);
    if (s.range == range) ready(c); else { c.ready = true; c.fetched_ms = esphome::millis(); }
  }

  // The 50 ms tick: reply timeout (one retry, then FAILED) and the LOADING
  // animation. True when the page has to be redrawn.
  static bool poll() {
    St& s = st();
    if (s.state != LOADING) return false;
    if (!s.want_send && olderThan(s.sent_ms, REPLY_TIMEOUT_MS)) {
      if (s.tries < MAX_TRIES) { s.want_send = true; s.sent_ms = esphome::millis(); return false; }
      fail(F_NO_REPLY);
      return true;
    }
    const uint32_t frame = esphome::millis() / 300;
    if (frame == s.anim) return false;
    s.anim = frame;
    return true;
  }

  static bool busy() { return st().state == LOADING; }

  // ── drawing (below the strip, above the soft keys) ────────────────────────

  template<class D>
  static void draw(D* it, int range, int interval_mins) {
    St& s = st();
    range = clampRange(range);
    const Series& c = s.series[range];
    const bool have = s.state == READY && c.ready;

    it->filled_rectangle(X0, Y0, W, 1, COLOR_ON);                  // baseline
    if (have) plot(it, c);
    for (int y = Y100; y <= Y50; y += Y50 - Y100)                   // dotted 100 % / 50 % lines, inverting
      for (int x = X0; x < X0 + W; x += 2) {                         // what they cross so they stay visible
        // the display has no read-back; the plot drew solid bars/contours, so xor by redrawing:
        it->draw_pixel_at(x, y, (have && lit(c, x, y)) ? COLOR_OFF : COLOR_ON);
      }
    rightLabel(it, Y100 - 2, "100%");
    rightLabel(it, Y50 - 2, "50%");
    rightLabel(it, Y0 - 2, "0%");
    if (have) axis(it, c);

    char line1[24] = "", line2[24] = "";
    if (s.state == LOADING) {
      snprintf(line1, sizeof(line1), "LOADING%.*s", int(s.anim % 4), "...");
    } else if (s.state == FAILED) {
      switch (s.fail) {
        case F_SEND:           snprintf(line1, sizeof(line1), "HUB NOT REACHED"); break;
        case F_NO_REPLY:       snprintf(line1, sizeof(line1), "NO ANSWER"); snprintf(line2, sizeof(line2), "FROM THE HUB"); break;
        case F_HA:             snprintf(line1, sizeof(line1), "HOME ASSISTANT"); snprintf(line2, sizeof(line2), "NOT REACHED"); break;
        case F_NO_TIME:        snprintf(line1, sizeof(line1), "WAITING FOR"); snprintf(line2, sizeof(line2), "THE CLOCK"); break;
        case F_NOT_CONFIGURED: snprintf(line1, sizeof(line1), "HISTORY NOT"); snprintf(line2, sizeof(line2), "SET UP"); break;
        default:               snprintf(line1, sizeof(line1), "NO HISTORY"); break;
      }
    } else if (have && validCount(c) == 0) {
      snprintf(line1, sizeof(line1), "NO HISTORY YET");
      if (interval_mins >= 60 && interval_mins % 60 == 0) snprintf(line2, sizeof(line2), "LOGGED EVERY %d H", interval_mins / 60);
      else                                                 snprintf(line2, sizeof(line2), "LOGGED EVERY %d MIN", interval_mins);
    }
    if (line1[0]) message(it, line2[0] ? 20 : 25, line1);
    if (line2[0]) message(it, 30, line2);
  }

private:
  struct Series {
    bool     ready{false};
    uint32_t fetched_ms{0};
    uint8_t  count{0};
    uint16_t bucket_min{60};
    uint32_t end_ts{0};
    int32_t  tz_s{0};
    uint8_t  values[hist::MAX_SAMPLES];
    uint8_t  valid[(hist::MAX_SAMPLES + 7) / 8];
  };
  struct St {
    Series   series[hist::RANGE_COUNT];
    int      range{0};
    State    state{IDLE};
    Fail     fail{F_NONE};
    int      tries{0};
    bool     want_send{false};
    uint8_t  req_id{0};
    uint32_t sent_ms{0};
    uint8_t  frag_mask{0};
    uint32_t anim{0};
  };
  static St& st() { static St s; return s; }

  static bool olderThan(uint32_t since, uint32_t ms) { return uint32_t(esphome::millis() - since) > ms; }

  static void ready(Series& c) {
    c.ready = true;
    c.fetched_ms = esphome::millis();
    st().state = READY;
  }

  static void fail(Fail f) {
    St& s = st();
    s.state = FAILED;
    s.fail = f;
    s.want_send = false;
  }

  static Fail fromStatus(uint8_t status) {
    switch (status) {
      case hist::ST_NO_TIME:        return F_NO_TIME;
      case hist::ST_NOT_CONFIGURED: return F_NOT_CONFIGURED;
      default:                      return F_HA;
    }
  }

  static bool isValid(const Series& c, int i) { return i >= 0 && i < c.count && (c.valid[i / 8] & (1 << (i % 8))); }
  static int  validCount(const Series& c) { int n = 0; for (int i = 0; i < c.count; i++) if (isValid(c, i)) n++; return n; }
  static int  height(uint8_t pct) { return (int(pct) * PLOT_H + 50) / 100; }
  static int  colX(const Series& c, int j) { return X0 + (j * W) / c.count; }   // left edge of column j, j = count → right edge
  static int  colAt(const Series& c, int x) {                                    // the column whose colX range holds x
    return ((x - X0 + 1) * c.count + W - 1) / W - 1;
  }
  static bool bars(const Series& c) { return W / c.count >= 3; }                 // wide enough for bars with a gap
  static bool charging(const Series& c, int i) { return isValid(c, i) && isValid(c, i - 1) && c.values[i] > c.values[i - 1] + 1; }

  // ── the level ─────────────────────────────────────────────────────────────
  // Bars fill their column up to a 1 px gap, so 24 bars spread over the whole
  // plot width (3 or 4 px each) instead of leaving the right end empty.

  template<class D>
  static void plot(D* it, const Series& c) {
    if (c.count == 0) return;
    if (bars(c)) {
      for (int i = 0; i < c.count; i++) {
        if (!isValid(c, i)) continue;
        const int h = height(c.values[i]);
        if (h <= 0) continue;
        const int x = colX(c, i), w = colX(c, i + 1) - x - 1;
        if (charging(c, i) && h >= 2) it->rectangle(x, Y0 - h, w, h, COLOR_ON);
        else                          it->filled_rectangle(x, Y0 - h, w, h, COLOR_ON);
      }
      it->filled_rectangle(colX(c, c.count - 1), Y0 + 1, colX(c, c.count) - colX(c, c.count - 1) - 1, 2, COLOR_ON);   // now
      return;
    }
    for (int i = 0; i < c.count; i++) {
      if (!isValid(c, i)) continue;
      const int top = Y0 - height(c.values[i]);
      const int xa = colX(c, i), xb = colX(c, i + 1);
      for (int x = xa; x < xb; x++) {
        it->draw_pixel_at(x, top, COLOR_ON);                                      // contour
        for (int y = top + 1; y < Y0; y++) if (((x + y) & 1) == 0) it->draw_pixel_at(x, y, COLOR_ON);
      }
      if (isValid(c, i - 1)) {                                                    // step to the previous column
        const int ptop = Y0 - height(c.values[i - 1]);
        const int y0 = top < ptop ? top : ptop, y1 = top < ptop ? ptop : top;
        it->filled_rectangle(xa, y0, 1, y1 - y0 + 1, COLOR_ON);
      }
    }
    it->filled_rectangle(colX(c, c.count - 1), Y0 + 1, colX(c, c.count) - colX(c, c.count - 1), 2, COLOR_ON);
  }

  // Whether plot() lit pixel (x, y) - the gridlines invert what they cross.
  static bool lit(const Series& c, int x, int y) {
    if (c.count == 0 || x < X0 || x >= X0 + W) return false;
    const int i = colAt(c, x);
    if (bars(c)) {
      const int xa = colX(c, i), xb = colX(c, i + 1) - 1;                        // bar spans xa..xb-1, xb is the gap
      if (x == xb || !isValid(c, i)) return false;
      const int h = height(c.values[i]);
      if (y < Y0 - h) return false;
      if (!(charging(c, i) && h >= 2)) return true;
      return y == Y0 - h || x == xa || x == xb - 1;                               // hollow bar: its frame
    }
    if (!isValid(c, i)) return false;
    const int top = Y0 - height(c.values[i]);
    if (y == top) return true;
    if (y > top && ((x + y) & 1) == 0) return true;
    if (x == colX(c, i) && isValid(c, i - 1)) {                                  // the step line
      const int ptop = Y0 - height(c.values[i - 1]);
      return y >= (top < ptop ? top : ptop) && y <= (top < ptop ? ptop : top);
    }
    return false;
  }

  // ── text helpers (3x5 font on a black box) ────────────────────────────────

  template<class D>
  static void label(D* it, int x, int y, const char* s) {
    it->filled_rectangle(x - 1, y - 1, ContextStrip::textWidth(s) + 2, 7, COLOR_OFF);
    ContextStrip::text(it, x, y, s, COLOR_ON);
  }

  // The % scale, right-aligned to the screen edge, right of the plot.
  template<class D>
  static void rightLabel(D* it, int y, const char* s) {
    ContextStrip::text(it, 128 - ContextStrip::textWidth(s), y, s, COLOR_ON);
  }

  template<class D>
  static void message(D* it, int y, const char* s) {
    const int w = ContextStrip::textWidth(s);
    label(it, X0 + (W - w) / 2, y, s);
  }

  // Label centred on x, kept inside the screen.
  template<class D>
  static void centred(D* it, int cx, const char* s, bool inverted = false) {
    const int w = ContextStrip::textWidth(s);
    int x = cx - w / 2;
    if (x < X0) x = X0;
    if (x + w > 128) x = 128 - w;
    if (inverted) {
      it->filled_rectangle(x - 1, AXIS_Y - 1, w + 2, 7, COLOR_ON);
      ContextStrip::text(it, x, AXIS_Y, s, COLOR_OFF);
    } else {
      ContextStrip::text(it, x, AXIS_Y, s, COLOR_ON);
    }
  }

  // ── time axis ─────────────────────────────────────────────────────────────

  template<class D>
  static void axis(D* it, const Series& c) {
    if (c.count == 0) return;
    const uint32_t bucket_s = uint32_t(c.bucket_min) * 60;
    char buf[8];
    const bool relative = c.end_ts == 0;

    if (c.bucket_min <= 60) {
      // hours: a label where a bucket starts at a multiple of 6 h (or every 6 columns back from now)
      for (int i = 0; i < c.count; i++) {
        const int back = c.count - 1 - i;
        if (relative) {
          if (back % 6 != 0) continue;
          if (back == 0) snprintf(buf, sizeof(buf), "NOW"); else snprintf(buf, sizeof(buf), "-%dH", back);
        } else {
          const hist::LocalTime t = hist::local_time(c.end_ts - uint32_t(c.count - i) * bucket_s, c.tz_s);
          if (t.hour % 6 != 0 || t.minute != 0) continue;
          snprintf(buf, sizeof(buf), "%02d", t.hour);
        }
        centred(it, (colX(c, i) + colX(c, i + 1) - 1) / 2, buf);
      }
      return;
    }

    if (c.bucket_min <= 180) {
      // days: one letter per local day, centred on its columns; today inverted
      if (relative) {
        const int per_day = 1440 / c.bucket_min;
        for (int i = c.count - 1; i >= 0; i -= per_day) {
          const int back = (c.count - 1 - i) / per_day;
          if (back == 0) snprintf(buf, sizeof(buf), "NOW"); else snprintf(buf, sizeof(buf), "-%dD", back);
          centred(it, (colX(c, i) + colX(c, i + 1) - 1) / 2, buf);
        }
        return;
      }
      static const char* LETTERS = "SMTWTFS";
      const int32_t today = hist::local_time(c.end_ts - 1, c.tz_s).days;
      int start = 0;
      int32_t day = hist::local_time(c.end_ts - uint32_t(c.count) * bucket_s, c.tz_s).days;   // day of column 0
      for (int i = 1; i <= c.count; i++) {
        const int32_t d = i < c.count ? hist::local_time(c.end_ts - uint32_t(c.count - i) * bucket_s, c.tz_s).days : -1;
        if (i < c.count && d == day) continue;
        if (i - start >= 3) {                                                     // enough columns for a letter
          buf[0] = LETTERS[hist::weekday_from_days(day)]; buf[1] = '\0';
          centred(it, (colX(c, start) + colX(c, i) - 1) / 2, buf, day == today);
        }
        start = i; day = d;
      }
      return;
    }

    // weeks: a tick (and a D.M date) at every local midnight a whole number of weeks before today
    for (int j = 0; j <= c.count; j++) {
      const int back = c.count - j;
      bool mark = false;
      if (relative) {
        const int per_week = 7 * 1440 / c.bucket_min;
        mark = back > 0 && back % per_week == 0;
        if (mark) snprintf(buf, sizeof(buf), "-%dW", back / per_week);
      } else {
        const uint32_t ts = c.end_ts - uint32_t(back) * bucket_s;
        const hist::LocalTime t = hist::local_time(ts, c.tz_s);
        const int32_t today = hist::local_time(c.end_ts - 1, c.tz_s).days;
        const int32_t days_back = today - t.days;
        mark = t.hour == 0 && t.minute == 0 && days_back > 0 && days_back % 7 == 0;
        if (mark) snprintf(buf, sizeof(buf), "%d.%d", t.day, t.month);
      }
      if (!mark) continue;
      const int x = colX(c, j);
      it->filled_rectangle(x, Y0 + 1, 1, 2, COLOR_ON);
      centred(it, x, buf);
    }
  }
};
