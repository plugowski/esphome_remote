#pragma once
#include <cstdint>
#include <cstdlib>
#include <cstring>

// ── Sensor history: pieces shared by the remote and the hub ──────────────────
// Pure C++, no ESPHome dependencies.
//
// Home Assistant's Recorder is the only store of a sensor's past values; the
// remote never keeps long-term history itself. Whoever talks to Home Assistant
// (the ESP-NOW hub, or the WiFi remote directly) fetches
//   GET /api/history/period/<start>?filter_entity_id=<e>&end_time=<end>
//       &minimal_response&no_attributes
// and resamples the state changes into fixed buckets:
//
//   • Buckets are aligned to local time (so a 6 h bucket ends at 00/06/12/18
//     local) and the last one is the bucket running now.
//   • A bucket's value is the state in force at its end: Home Assistant
//     states are step functions, so a reading that did not change for hours
//     still counts, while an "unavailable" / "unknown" state, or no state at
//     all yet, leaves the bucket empty. Empty is a gap, never 0.
//
// hist::Range lists what the battery screen asks for; the request itself is
// generic (entity, bucket length, count), so other sensors can use it later.

namespace hist {

static const int MAX_SAMPLES = 120;

struct Range {
  const char* label;     // context badge on the screen
  uint16_t    bucket_min;
  uint8_t     count;
};
// 24 h of hourly samples, 7 days of 2 h, 30 days of 6 h.
static const Range RANGES[] = { {"24H", 60, 24}, {"7D", 120, 84}, {"30D", 360, 120} };
static const int RANGE_COUNT = sizeof(RANGES) / sizeof(RANGES[0]);

// HIST_DATA status byte.
enum Status : uint8_t {
  ST_OK             = 0,
  ST_HA_ERROR       = 1,   // Home Assistant unreachable or it answered with an error
  ST_NO_TIME        = 2,   // the fetching side has no valid clock yet
  ST_NOT_CONFIGURED = 3,   // no Home Assistant URL / token configured
};

// ── calendar (proleptic Gregorian, days since 1970-01-01) ────────────────────

inline int32_t days_from_civil(int y, int m, int d) {
  y -= m <= 2;
  const int32_t era = (y >= 0 ? y : y - 399) / 400;
  const int32_t yoe = y - era * 400;
  const int32_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const int32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

inline void civil_from_days(int32_t z, int& y, int& m, int& d) {
  z += 719468;
  const int32_t era = (z >= 0 ? z : z - 146096) / 146097;
  const int32_t doe = z - era * 146097;
  const int32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const int32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const int32_t mp  = (5 * doy + 2) / 153;
  d = doy - (153 * mp + 2) / 5 + 1;
  m = mp + (mp < 10 ? 3 : -9);
  y = yoe + era * 400 + (m <= 2);
}

// 0 = Sunday .. 6 = Saturday
inline int weekday_from_days(int32_t z) { return (z + 4) % 7 < 0 ? (z + 4) % 7 + 7 : (z + 4) % 7; }

struct LocalTime {
  int year, month, day, hour, minute, wday;
  int32_t days;   // local days since the epoch
};

inline LocalTime local_time(uint32_t utc, int32_t tz_offset_s) {
  LocalTime t;
  const int64_t local = int64_t(utc) + tz_offset_s;
  t.days = int32_t(local >= 0 ? local / 86400 : (local - 86399) / 86400);
  const int32_t secs = int32_t(local - int64_t(t.days) * 86400);
  civil_from_days(t.days, t.year, t.month, t.day);
  t.wday   = weekday_from_days(t.days);
  t.hour   = secs / 3600;
  t.minute = (secs % 3600) / 60;
  return t;
}

// End of the bucket that contains `now`, buckets being aligned to local time.
inline uint32_t bucket_end(uint32_t now_utc, int32_t tz_offset_s, uint32_t bucket_s) {
  const int64_t local = int64_t(now_utc) + tz_offset_s;
  const int64_t end   = (local / int64_t(bucket_s) + 1) * int64_t(bucket_s);
  return uint32_t(end - tz_offset_s);
}

// "2026-10-27T10:00:00.123456+00:00" / "...Z" → UTC epoch. False on anything else.
inline bool parse_iso8601(const char* s, uint32_t& out) {
  const char* p = s;
  auto num = [&p](int digits, int& v) {
    v = 0;
    for (int i = 0; i < digits; i++) {
      if (*p < '0' || *p > '9') return false;
      v = v * 10 + (*p++ - '0');
    }
    return true;
  };
  int y, mo, d, h, mi, se;
  if (!num(4, y) || *p++ != '-' || !num(2, mo) || *p++ != '-' || !num(2, d)) return false;
  if (*p != 'T' && *p != ' ') return false;
  p++;
  if (!num(2, h) || *p++ != ':' || !num(2, mi) || *p++ != ':' || !num(2, se)) return false;
  if (*p == '.') { p++; while (*p >= '0' && *p <= '9') p++; }
  int32_t off = 0;
  if (*p == '+' || *p == '-') {
    const int sign = *p++ == '-' ? -1 : 1;
    int oh, om;
    if (!num(2, oh)) return false;
    if (*p == ':') p++;
    if (!num(2, om)) return false;
    off = sign * (oh * 3600 + om * 60);
  } else if (*p != 'Z' && *p != '\0') {
    return false;
  }
  const int64_t t = int64_t(days_from_civil(y, mo, d)) * 86400 + h * 3600 + mi * 60 + se - off;
  if (t < 0 || t > 0xFFFFFFFFLL) return false;
  out = uint32_t(t);
  return true;
}

// "2026-10-27T10:00:00Z" for the history URL.
inline void format_iso8601(uint32_t utc, char* buf, size_t len) {
  const LocalTime t = local_time(utc, 0);
  snprintf(buf, len, "%04d-%02d-%02dT%02d:%02d:%02dZ", t.year, t.month, t.day, t.hour, t.minute,
           int(utc % 60));
}

// ── Resampler ────────────────────────────────────────────────────────────────
// Takes the entity's state changes in time order and fills `count` buckets
// ending at `end_ts`. See the header comment for the rules.

class Resampler {
public:
  void begin(uint32_t end_ts, uint32_t bucket_s, uint8_t count) {
    end_ = end_ts; bucket_ = bucket_s; count_ = count > MAX_SAMPLES ? MAX_SAMPLES : count;
    cur_ = 0; have_ = false; valid_now_ = false; value_now_ = 0; fed_ = 0;
    memset(values_, 0, sizeof(values_));
    memset(valid_, 0, sizeof(valid_));
  }

  // One state change: `valid` false for unavailable / unknown / non-numeric.
  void feed(uint32_t ts, bool valid, uint8_t value) {
    // every bucket that ended before this change keeps the state before it
    while (cur_ < count_ && bucketEnd(cur_) < ts) commit(cur_++);
    have_ = true; valid_now_ = valid; value_now_ = value;
    fed_++;
  }

  void finish() { while (cur_ < count_) commit(cur_++); }

  uint32_t bucketEnd(int i) const { return end_ - uint32_t(count_ - 1 - i) * bucket_; }
  uint8_t  count()   const { return count_; }
  uint32_t endTs()   const { return end_; }
  uint32_t fed()     const { return fed_; }
  const uint8_t* values() const { return values_; }
  const uint8_t* valid()  const { return valid_; }

private:
  void commit(int i) {
    if (!have_ || !valid_now_) return;
    values_[i] = value_now_;
    valid_[i / 8] |= 1 << (i % 8);
  }

  uint32_t end_{0}, bucket_{3600}, fed_{0};
  uint8_t  count_{0};
  int      cur_{0};
  bool     have_{false}, valid_now_{false};
  uint8_t  value_now_{0};
  uint8_t  values_[MAX_SAMPLES];
  uint8_t  valid_[(MAX_SAMPLES + 7) / 8];
};

// "87", "87.0" → 87; unavailable / unknown / anything else → false.
inline bool parse_percent(const char* s, uint8_t& out) {
  if (*s == '\0' || (*s < '0' || *s > '9')) return false;
  char* end = nullptr;
  const float f = strtof(s, &end);
  if (end == s || f < 0) return false;
  out = uint8_t(f > 100 ? 100 : f + 0.5f);
  return true;
}

// ── HaHistoryScanner ─────────────────────────────────────────────────────────
// Streams the history JSON through, without buffering it: a 30-day answer is
// tens of kilobytes. The payload is
//   [ [ {"last_changed": "...", "state": "87", ...}, ... ] ]
// so every object at nesting depth 3 is one state. Only its own "state" and
// "last_changed" keys are looked at - "attributes" / "context" objects one
// level deeper are skipped - and when it closes the pair goes to the
// Resampler. Any other key order works.

class HaHistoryScanner {
public:
  explicit HaHistoryScanner(Resampler& r) : r_(r) {}

  void feed(const uint8_t* data, size_t n) {
    for (size_t i = 0; i < n; i++) step(char(data[i]));
  }

  uint32_t states() const { return states_; }   // objects handed to the resampler

private:
  static const int ENTRY_DEPTH = 3;
  enum Key : uint8_t { K_OTHER, K_STATE, K_CHANGED };

  void step(char c) {
    if (in_string_) {
      if (escape_) { escape_ = false; put(c); return; }
      if (c == '\\') { escape_ = true; return; }
      if (c == '"') { in_string_ = false; endString(); return; }
      put(c);
      return;
    }
    switch (c) {
      case '"': in_string_ = true; len_ = 0; break;
      case ':': if (depth_ == ENTRY_DEPTH) expect_value_ = true; break;
      case ',': expect_value_ = false; key_ = K_OTHER; break;
      case '{': case '[':
        depth_++;
        expect_value_ = false;
        if (c == '{' && depth_ == ENTRY_DEPTH) { have_state_ = have_ts_ = false; }
        break;
      case '}': case ']':
        if (c == '}' && depth_ == ENTRY_DEPTH && have_ts_) {
          r_.feed(ts_, have_state_ && state_ok_, state_);
          states_++;
        }
        if (depth_ > 0) depth_--;
        expect_value_ = false;
        break;
      default: break;
    }
  }

  void put(char c) { if (len_ < sizeof(buf_) - 1) buf_[len_++] = c; }

  void endString() {
    buf_[len_] = '\0';
    if (depth_ != ENTRY_DEPTH) return;
    if (!expect_value_) {                       // a key
      key_ = strcmp(buf_, "state") == 0 ? K_STATE : strcmp(buf_, "last_changed") == 0 ? K_CHANGED : K_OTHER;
      return;
    }
    if (key_ == K_STATE)        { have_state_ = true; state_ok_ = parse_percent(buf_, state_); }
    else if (key_ == K_CHANGED) { have_ts_ = parse_iso8601(buf_, ts_); }
    expect_value_ = false;
    key_ = K_OTHER;
  }

  Resampler& r_;
  char     buf_[40];
  size_t   len_{0};
  int      depth_{0};
  bool     in_string_{false}, escape_{false}, expect_value_{false};
  Key      key_{K_OTHER};
  bool     have_state_{false}, state_ok_{false}, have_ts_{false};
  uint8_t  state_{0};
  uint32_t ts_{0};
  uint32_t states_{0};
};

}  // namespace hist
