#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// ── ESP-NOW remote protocol ───────────────────────────────────────────────────
// Shared between the ESP-NOW remote (remote_espnow.yaml) and the hub
// (devices/espnow_hub).  Pure C++, no ESPHome dependencies.
//
// Every packet starts with an 8-byte header:
//   'R' 'N'  version  type  key[4] (little-endian uint32 network key)
//
// Packet types (remote → hub):
//   HELLO    : proto_version u8, flags u8 (bit0 = request full state)
//   CMD      : domain u8, idx u8, action u8, value i16
//   BATTERY  : pct u8, millivolts u16
//   HIST_REQ : req_id u8, bucket_minutes u16, count u8 (1-120), entity str
//              - asks for the entity's Home Assistant history, resampled
//              into `count` buckets of `bucket_minutes` ending now
//   SCREENSHOT_DATA : chunk u8 (0-7), then 128 raw framebuffer bytes
//                     (page `chunk` of the 128×64 OLED buffer)
//
// Packet types (hub → remote):
//   STATE_AC     : count u8, then per unit:  idx u8, temp i8 (-1 unknown),
//                                            mode str, fan str
//   STATE_LIGHTS : count u8, then per light: idx u8, on u8 (0/1, 2 unknown),
//                                            brightness u8 (0-100, 255 unknown)
//   STATE_COVERS : count u8, then per cover: idx u8, state str,
//                                            position u8 (0-100, 255 unknown)
//   STATE_QUICK  : count u8, then per toggle quick action: idx u8 (into
//                  QUICK_ACTION_LIST), on u8 (0/1, 2 unknown) - only the
//                  *.toggle actions are listed
//   STATE_DONE   : (no payload) — ends a full snapshot after HELLO
//   SCREENSHOT_REQ : (no payload) — remote answers with 8 SCREENSHOT_DATA
//   HIST_DATA    : req_id u8, status u8 (hist::Status), bucket_minutes u16,
//                  count u8, end_ts u32 (UTC epoch, end of the last bucket),
//                  tz_offset i16 (minutes to add to UTC for local time),
//                  frag u8, frag_count u8, offset u8, n u8,
//                  valid bitmap ((n+7)/8 bytes, bit k = sample offset+k is
//                  present), then n sample bytes (u8, 0-100). A sample
//                  whose bit is clear is a gap, never a value. The whole
//                  series is split into fragments of at most
//                  HIST_FRAG_SAMPLES; status != 0 comes with count = 0.
//
// Strings are length-prefixed (u8 length, max 63 bytes, no terminator).
// Max ESP-NOW payload is 250 bytes; the hub splits long state lists into
// several packets of the same type (each is self-contained).

namespace rnp {

static const uint8_t MAGIC0  = 'R';
static const uint8_t MAGIC1  = 'N';
static const uint8_t VERSION = 1;

static const size_t MAX_PACKET = 250;
static const size_t HEADER_LEN = 8;
static const size_t MAX_STR    = 63;

enum PacketType : uint8_t {
  HELLO        = 0x01,
  STATE_AC     = 0x02,
  STATE_LIGHTS = 0x03,
  STATE_COVERS = 0x04,
  STATE_DONE   = 0x05,
  SCREENSHOT_REQ  = 0x06,
  HIST_DATA    = 0x07,
  STATE_QUICK  = 0x08,
  CMD          = 0x10,
  BATTERY      = 0x20,
  HIST_REQ     = 0x21,
  SCREENSHOT_DATA = 0x30,
};

static const size_t SCREENSHOT_CHUNKS     = 8;
static const size_t SCREENSHOT_CHUNK_SIZE = 128;
// Samples per HIST_DATA fragment: 200 values + a 25-byte bitmap + the
// 15-byte fixed part stay under MAX_PACKET.
static const size_t HIST_FRAG_SAMPLES = 200;

enum Domain : uint8_t {
  DOM_AC          = 0,
  DOM_LIGHTS      = 1,
  DOM_COVERS      = 2,
  DOM_AUTOMATIONS = 3,
  DOM_QUICK       = 4,
};

// value semantics per action:
//   AC_SET_MODE  → index into AC_LIST[idx].modes, or AC_MODE_OFF (255) for "off"
//   AC_SET_FAN   → index into AC_LIST[idx].fan_modes
//   AC_SET_TEMP  → target temperature in °C
enum AcAction : uint8_t { AC_SET_MODE = 0, AC_SET_FAN = 1, AC_SET_TEMP = 2 };
static const int16_t AC_MODE_OFF = 255;

enum LightAction : uint8_t { LIGHT_TOGGLE = 0, LIGHT_SET_BRIGHTNESS = 1,     // value = pct
                             LIGHT_ON = 2, LIGHT_OFF = 3 };
enum CoverAction : uint8_t { COVER_OPEN = 0, COVER_CLOSE = 1, COVER_STOP = 2, COVER_SET_POSITION = 3 };
enum AutoAction  : uint8_t { AUTO_TRIGGER = 0 };                             // idx into list

static const uint8_t LIGHT_UNKNOWN = 2;
static const uint8_t VALUE_UNKNOWN = 255;

static const uint8_t BROADCAST_ADDR[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

// ── Writer ────────────────────────────────────────────────────────────────────
class Writer {
public:
  explicit Writer(uint32_t key, uint8_t type) { begin(key, type); }

  void begin(uint32_t key, uint8_t type) {
    buf_.clear();
    buf_.reserve(64);
    ok_ = true;
    buf_.push_back(MAGIC0);
    buf_.push_back(MAGIC1);
    buf_.push_back(VERSION);
    buf_.push_back(type);
    u32(key);
  }

  Writer& u8(uint8_t v)  { push_(&v, 1); return *this; }
  Writer& i8(int8_t v)   { push_(reinterpret_cast<uint8_t*>(&v), 1); return *this; }
  Writer& u16(uint16_t v){ uint8_t b[2] = { uint8_t(v & 0xFF), uint8_t(v >> 8) }; push_(b, 2); return *this; }
  Writer& i16(int16_t v) { return u16(static_cast<uint16_t>(v)); }
  Writer& u32(uint32_t v){
    uint8_t b[4] = { uint8_t(v & 0xFF), uint8_t((v >> 8) & 0xFF), uint8_t((v >> 16) & 0xFF), uint8_t(v >> 24) };
    push_(b, 4); return *this;
  }
  Writer& str(const std::string& s) {
    size_t n = s.size() > MAX_STR ? MAX_STR : s.size();
    u8(static_cast<uint8_t>(n));
    push_(reinterpret_cast<const uint8_t*>(s.data()), n);
    return *this;
  }
  Writer& str(const char* s) { return str(std::string(s ? s : "")); }

  // Size the next record would need; use to decide when to split packets.
  bool fits(size_t extra) const { return buf_.size() + extra <= MAX_PACKET; }
  bool ok() const { return ok_; }
  size_t size() const { return buf_.size(); }
  const std::vector<uint8_t>& data() const { return buf_; }
  std::vector<uint8_t> take() { return buf_; }

private:
  void push_(const uint8_t* p, size_t n) {
    if (buf_.size() + n > MAX_PACKET) { ok_ = false; return; }
    buf_.insert(buf_.end(), p, p + n);
  }
  std::vector<uint8_t> buf_;
  bool ok_{true};
};

// ── Reader ────────────────────────────────────────────────────────────────────
class Reader {
public:
  Reader(const uint8_t* data, size_t size) : p_(data), n_(size), pos_(HEADER_LEN) {}

  // Validates magic, version and key.  Returns packet type or 0 on failure.
  static uint8_t check(const uint8_t* d, size_t n, uint32_t key) {
    if (d == nullptr || n < HEADER_LEN) return 0;
    if (d[0] != MAGIC0 || d[1] != MAGIC1 || d[2] != VERSION) return 0;
    uint32_t k = uint32_t(d[4]) | (uint32_t(d[5]) << 8) | (uint32_t(d[6]) << 16) | (uint32_t(d[7]) << 24);
    if (k != key) return 0;
    return d[3];
  }

  bool u8(uint8_t& v)   { if (pos_ + 1 > n_) return false; v = p_[pos_++]; return true; }
  bool i8(int8_t& v)    { uint8_t t; if (!u8(t)) return false; v = static_cast<int8_t>(t); return true; }
  bool u16(uint16_t& v) { if (pos_ + 2 > n_) return false; v = uint16_t(p_[pos_]) | (uint16_t(p_[pos_ + 1]) << 8); pos_ += 2; return true; }
  bool i16(int16_t& v)  { uint16_t t; if (!u16(t)) return false; v = static_cast<int16_t>(t); return true; }
  bool u32(uint32_t& v) { uint16_t lo, hi; if (!u16(lo) || !u16(hi)) return false; v = uint32_t(lo) | (uint32_t(hi) << 16); return true; }
  bool str(std::string& s) {
    uint8_t n; if (!u8(n)) return false;
    if (pos_ + n > n_) return false;
    s.assign(reinterpret_cast<const char*>(p_ + pos_), n);
    pos_ += n;
    return true;
  }
  bool done() const { return pos_ >= n_; }

private:
  const uint8_t* p_;
  size_t n_;
  size_t pos_;
};

// ── Convenience builders (remote → hub) ───────────────────────────────────────
inline std::vector<uint8_t> build_hello(uint32_t key, bool want_state = true) {
  Writer w(key, HELLO);
  w.u8(VERSION).u8(want_state ? 1 : 0);
  return w.take();
}

inline std::vector<uint8_t> build_cmd(uint32_t key, uint8_t domain, uint8_t idx, uint8_t action, int16_t value) {
  Writer w(key, CMD);
  w.u8(domain).u8(idx).u8(action).i16(value);
  return w.take();
}

inline std::vector<uint8_t> build_battery(uint32_t key, int pct, int millivolts) {
  Writer w(key, BATTERY);
  if (pct < 0) pct = 0; if (pct > 100) pct = 100;
  if (millivolts < 0) millivolts = 0; if (millivolts > 65535) millivolts = 65535;
  w.u8(static_cast<uint8_t>(pct)).u16(static_cast<uint16_t>(millivolts));
  return w.take();
}

inline std::vector<uint8_t> build_hist_req(uint32_t key, uint8_t req_id, uint16_t bucket_minutes, uint8_t count,
                                           const char* entity) {
  Writer w(key, HIST_REQ);
  w.u8(req_id).u16(bucket_minutes).u8(count).str(entity);
  return w.take();
}

// One fragment of a history reply: samples [offset, offset + n) of the
// series described by `values` / `valid` (bit i of valid = sample i present).
inline std::vector<uint8_t> build_hist_data(uint32_t key, uint8_t req_id, uint8_t status, uint16_t bucket_minutes,
                                            uint8_t count, uint32_t end_ts, int16_t tz_minutes,
                                            uint8_t frag, uint8_t frag_count, uint8_t offset, uint8_t n,
                                            const uint8_t* values, const uint8_t* valid) {
  Writer w(key, HIST_DATA);
  w.u8(req_id).u8(status).u16(bucket_minutes).u8(count).u32(end_ts).i16(tz_minutes);
  w.u8(frag).u8(frag_count).u8(offset).u8(n);
  for (size_t b = 0; b < (size_t(n) + 7) / 8; b++) {
    uint8_t bits = 0;
    for (size_t k = 0; k < 8 && b * 8 + k < n; k++) {
      const size_t i = offset + b * 8 + k;
      if (valid[i / 8] & (1 << (i % 8))) bits |= 1 << k;
    }
    w.u8(bits);
  }
  for (size_t k = 0; k < n; k++) w.u8(values[offset + k]);
  return w.take();
}

inline std::vector<uint8_t> build_state_done(uint32_t key) {
  Writer w(key, STATE_DONE);
  return w.take();
}

inline std::vector<uint8_t> build_screenshot_req(uint32_t key) {
  Writer w(key, SCREENSHOT_REQ);
  return w.take();
}

// `buf` is the whole 1024-byte framebuffer; only page `chunk` is sent.
inline std::vector<uint8_t> build_screenshot_data(uint32_t key, uint8_t chunk, const uint8_t* buf) {
  Writer w(key, SCREENSHOT_DATA);
  w.u8(chunk);
  for (size_t i = 0; i < SCREENSHOT_CHUNK_SIZE; i++) w.u8(buf[chunk * SCREENSHOT_CHUNK_SIZE + i]);
  return w.take();
}

}  // namespace rnp
