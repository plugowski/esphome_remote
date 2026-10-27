#include "remote_hub.h"

#include <algorithm>
#include <functional>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>

#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "esphome/components/api/api_server.h"
#include "esphome/components/espnow/espnow_component.h"

namespace esphome {
namespace remote_hub {

static const char *const TAG = "remote_hub";
static const uint32_t PUSH_DEBOUNCE_MS = 150;
// AC commands are applied this long after the last one; every new command restarts the wait, so a
// run of presses (temperature taps, a quick ON then OFF) ends up as one change.
static const uint32_t SETTLE_MS = 900;
static const uint32_t STEP_MS = 700;         // pause between a mode change and the fan / temperature
static const uint32_t EXPECT_TTL_MS = 6000;  // how long a commanded value stands in for HA's state
// Brightness presses are applied this long after the last one (shorter than SETTLE_MS: a
// light has no mode / fan sequencing to wait for, and a dimmer should feel quick).
static const uint32_t LIGHT_SETTLE_MS = 500;
// A cover's final state is announced again this long after its last change (see cover_changed_).
static const uint32_t COVER_REPEAT_MS = 1500;
static const uint32_t EMU_STEP_MS = 250;  // cover emulator: one 5 % step this often
// Resend the screenshot request on the next packet if no image arrived by then.
static const uint32_t SCREENSHOT_RETRY_MS = 5000;

// A quick action with on / off state to show: service *.toggle (switch.toggle,
// input_boolean.toggle) - the same rule as QuickActionState::isToggle on the remote.
static bool is_toggle_(const char *service) {
  static const char SUFFIX[] = ".toggle";
  const size_t n = service ? strlen(service) : 0, k = sizeof(SUFFIX) - 1;
  return n > k && strcmp(service + n - k, SUFFIX) == 0;
}

static bool older_than(uint32_t since, uint32_t ms) { return (uint32_t) (millis() - since) > ms; }
using state_cb_t = std::function<void(const std::string &)>;

// ── lifecycle ─────────────────────────────────────────────────────────────────

void RemoteHub::setup() {
  if (this->cover_emu_) {
    // start the emulated covers at different positions so every state is easy to reach
    static const uint8_t START[3] = {50, 0, 100};
    for (int i = 0; i < COVER_LIST_COUNT; i++) {
      this->covers_[i].position = START[i % 3];
      this->covers_[i].state = this->covers_[i].position == 0 ? "closed" : "open";
    }
    ESP_LOGW(TAG, "Cover emulator ON: %d covers simulated on the hub, not taken from Home Assistant",
             COVER_LIST_COUNT);
  }
  this->subscribe_all_();
  ESP_LOGI(TAG, "Subscribed to %d AC, %d lights, %d covers; %d automations, %d quick actions",
           AC_LIST_COUNT, LIGHTS_LIST_COUNT, COVER_LIST_COUNT, AUTOMATION_LIST_COUNT, QUICK_ACTION_COUNT);
}

void RemoteHub::loop() {
  for (int i = 0; i < AC_LIST_COUNT; i++) {
    AcGuard &g = this->ac_guard_[i];
    if (g.due && (int32_t) (millis() - g.due_at) >= 0) this->apply_ac_(i);
    // A commanded value that HA never confirmed (the unit clamped or ignored it) stops counting
    // as the state; push the real one to the remotes.
    bool expired = false;
    if (g.repeat && (int32_t) (millis() - g.repeat_at) >= 0) {
      g.repeat = false;
      this->send_ac_(rnp::BROADCAST_ADDR);
      if (this->have_src_) this->send_ac_(this->last_src_);
    }
    if (g.exp_mode && older_than(g.exp_mode_at, EXPECT_TTL_MS)) {
      ESP_LOGW(TAG, "AC %d: HA still reports mode '%s' after commanding '%s'", i, this->ac_[i].mode.c_str(),
               g.exp_mode_v.c_str());
      g.exp_mode = false;
      expired = true;
    }
    if (g.exp_fan && older_than(g.exp_fan_at, EXPECT_TTL_MS)) { g.exp_fan = false; expired = true; }
    if (g.exp_temp && older_than(g.exp_temp_at, EXPECT_TTL_MS)) { g.exp_temp = false; expired = true; }
    if (expired) this->mark_dirty_(DIRTY_AC);
  }
  if (this->cover_emu_) this->step_emulated_covers_();
  for (int i = 0; i < LIGHTS_LIST_COUNT; i++) {
    LightGuard &g = this->light_guard_[i];
    if (g.want && (int32_t) (millis() - g.due_at) >= 0) this->apply_light_brightness_(i);
    if (g.exp && older_than(g.exp_at, EXPECT_TTL_MS)) {
      ESP_LOGW(TAG, "Light %d: HA still reports brightness %u after commanding %u", i,
               this->lights_[i].brightness, g.exp_v);
      g.exp = false;
      this->mark_dirty_(DIRTY_LIGHTS);
    }
  }
  if (this->covers_repeat_ && (int32_t) (millis() - this->covers_repeat_at_) >= 0) {
    this->covers_repeat_ = false;
    this->send_covers_(rnp::BROADCAST_ADDR);
    if (this->have_rx_src_) this->send_covers_(this->last_rx_src_);
  }
  if (this->dirty_ == 0) return;
  if (millis() - this->dirty_since_ < PUSH_DEBOUNCE_MS) return;
  uint8_t d = this->dirty_;
  this->dirty_ = 0;
  if (d & DIRTY_AC) this->send_ac_(rnp::BROADCAST_ADDR);
  if (d & DIRTY_LIGHTS) this->send_lights_(rnp::BROADCAST_ADDR);
  if (d & DIRTY_COVERS) this->send_covers_(rnp::BROADCAST_ADDR);
  if (d & DIRTY_QUICK) this->send_quick_(rnp::BROADCAST_ADDR);
}

// Sends the held brightness for light i to Home Assistant; it stays the reported value until
// HA confirms it or EXPECT_TTL_MS pass.
void RemoteHub::apply_light_brightness_(int i) {
  LightGuard &g = this->light_guard_[i];
  g.want = false;
  g.exp = true;
  g.exp_v = g.want_v;
  g.exp_at = millis();
  ESP_LOGI(TAG, "Light %d: brightness %u%%", i, g.want_v);
  this->call_homeassistant_service("light.turn_on", {{"entity_id", std::string(LIGHTS_LIST[i].entity_id)},
                                                     {"brightness_pct", std::to_string((int) g.want_v)}});
}

// ── cover emulator (testing aid) ─────────────────────────────────────────────

void RemoteHub::emulate_cover_(int i, uint8_t action, int value) {
  CoverState &c = this->covers_[i];
  EmuCover &e = this->emu_[i];
  const uint8_t cur = c.position == rnp::VALUE_UNKNOWN ? 0 : c.position;
  if (action == rnp::COVER_STOP) {
    e.moving = false;
    c.state = cur == 0 ? "closed" : "open";
  } else {
    int t = action == rnp::COVER_OPEN ? 100 : action == rnp::COVER_CLOSE ? 0 : value;
    if (t < 0) t = 0;
    if (t > 100) t = 100;
    e.target = static_cast<uint8_t>(t);
    e.moving = e.target != cur;
    e.last_step = millis();
    if (e.moving) c.state = e.target > cur ? "opening" : "closing";
  }
  ESP_LOGI(TAG, "Emulated cover %d: %s at %u%% -> %u%%", i, c.state.c_str(), cur, e.target);
  this->cover_changed_();
}

void RemoteHub::step_emulated_covers_() {
  for (int i = 0; i < COVER_LIST_COUNT; i++) {
    EmuCover &e = this->emu_[i];
    if (!e.moving || !older_than(e.last_step, EMU_STEP_MS)) continue;
    e.last_step = millis();
    CoverState &c = this->covers_[i];
    int p = c.position == rnp::VALUE_UNKNOWN ? 0 : c.position;
    p += (e.target > p) ? 5 : -5;
    if ((e.target > c.position && p >= e.target) || (e.target < c.position && p <= e.target)) p = e.target;
    c.position = static_cast<uint8_t>(p);
    if (c.position == e.target) {
      e.moving = false;
      c.state = c.position == 0 ? "closed" : "open";
    }
    this->cover_changed_();
  }
}

void RemoteHub::dump_config() {
  ESP_LOGCONFIG(TAG, "Remote Hub:");
  ESP_LOGCONFIG(TAG, "  Network key: %u", (unsigned) this->key_);
  ESP_LOGCONFIG(TAG, "  AC units: %d  lights: %d  covers: %d", AC_LIST_COUNT, LIGHTS_LIST_COUNT, COVER_LIST_COUNT);
  ESP_LOGCONFIG(TAG, "  Automations: %d  quick actions: %d", AUTOMATION_LIST_COUNT, QUICK_ACTION_COUNT);
}

void RemoteHub::mark_dirty_(uint8_t bit) {
  if (this->dirty_ == 0) this->dirty_since_ = millis();
  this->dirty_ |= bit;
}

// Covers report every step of a movement and each one goes out as an unacknowledged broadcast; a
// lost last one (OPEN 100 after OPENING 98) would leave the remote showing the cover still moving.
// So once the reports stop, the result is sent again: broadcast, and unicast to the remote heard
// last (acknowledged and retried by the radio).
void RemoteHub::cover_changed_() {
  this->mark_dirty_(DIRTY_COVERS);
  this->covers_repeat_ = true;
  this->covers_repeat_at_ = millis() + COVER_REPEAT_MS;
}

// ── Home Assistant subscriptions ──────────────────────────────────────────────

uint8_t RemoteHub::pct_from_ha_(const std::string &v, float scale) {
  if (v.empty() || v == "unknown" || v == "unavailable" || v == "None") return rnp::VALUE_UNKNOWN;
  float f = strtof(v.c_str(), nullptr) * scale;
  if (std::isnan(f) || f < 0) return rnp::VALUE_UNKNOWN;
  if (f > 100) f = 100;
  return static_cast<uint8_t>(f + 0.5f);
}

void RemoteHub::subscribe_all_() {
  auto *api = api::global_api_server;
  if (api == nullptr) {
    ESP_LOGE(TAG, "API server not available");
    return;
  }

  for (int i = 0; i < AC_LIST_COUNT; i++) {
    std::string eid = AC_LIST[i].entity_id;
    api->subscribe_home_assistant_state(eid, optional<std::string>{}, state_cb_t([this, i](const std::string &s) {
      if (this->ac_[i].mode != s) { this->ac_[i].mode = s; this->mark_dirty_(DIRTY_AC); }
      this->confirm_ac_expected_(i);
    }));
    api->subscribe_home_assistant_state(eid, optional<std::string>{"fan_mode"}, state_cb_t([this, i](const std::string &s) {
      if (this->ac_[i].fan != s) { this->ac_[i].fan = s; this->mark_dirty_(DIRTY_AC); }
      this->confirm_ac_expected_(i);
    }));
    api->subscribe_home_assistant_state(eid, optional<std::string>{"temperature"}, state_cb_t([this, i](const std::string &s) {
      int8_t t = -1;
      if (!s.empty() && s != "unknown" && s != "unavailable" && s != "None") {
        float f = strtof(s.c_str(), nullptr);
        if (!std::isnan(f) && f >= 0 && f < 127) t = static_cast<int8_t>(f + 0.5f);
      }
      // Keep the last known setpoint when HA reports none (null while the unit is off or in a
      // mode without one): the remote compares its selection with this value, and an "unknown"
      // would never match, so it would keep resending the command.
      if (t < 0 && this->ac_[i].temp >= 0) return;
      if (this->ac_[i].temp != t) { this->ac_[i].temp = t; this->mark_dirty_(DIRTY_AC); }
      this->confirm_ac_expected_(i);
    }));
  }

  for (int i = 0; i < LIGHTS_LIST_COUNT; i++) {
    std::string eid = LIGHTS_LIST[i].entity_id;
    api->subscribe_home_assistant_state(eid, optional<std::string>{}, state_cb_t([this, i](const std::string &s) {
      uint8_t on = (s == "on") ? 1 : (s == "off") ? 0 : rnp::LIGHT_UNKNOWN;
      if (this->lights_[i].on != on) { this->lights_[i].on = on; this->mark_dirty_(DIRTY_LIGHTS); }
    }));
    if (LIGHTS_LIST[i].dimmable) {
      api->subscribe_home_assistant_state(eid, optional<std::string>{"brightness"}, state_cb_t([this, i](const std::string &s) {
        uint8_t b = pct_from_ha_(s, 100.0f / 255.0f);
        if (this->lights_[i].brightness != b) { this->lights_[i].brightness = b; this->mark_dirty_(DIRTY_LIGHTS); }
        // HA reached the commanded brightness (±1 for the 0-255 rounding): stop standing in for it
        LightGuard &g = this->light_guard_[i];
        if (g.exp && !g.want && b != rnp::VALUE_UNKNOWN && (b + 1 >= g.exp_v && b <= g.exp_v + 1)) g.exp = false;
      }));
    }
  }

  for (int i = 0; i < QUICK_ACTION_CAPACITY; i++) this->quick_on_[i] = rnp::LIGHT_UNKNOWN;
  for (int i = 0; i < QUICK_ACTION_COUNT; i++) {
    if (!is_toggle_(QUICK_ACTION_LIST[i].service)) continue;
    std::string eid = QUICK_ACTION_LIST[i].entity_id;
    api->subscribe_home_assistant_state(eid, optional<std::string>{}, state_cb_t([this, i](const std::string &s) {
      uint8_t on = (s == "on") ? 1 : (s == "off") ? 0 : rnp::LIGHT_UNKNOWN;
      if (this->quick_on_[i] != on) { this->quick_on_[i] = on; this->mark_dirty_(DIRTY_QUICK); }
    }));
  }

  for (int i = 0; i < COVER_LIST_COUNT && !this->cover_emu_; i++) {
    std::string eid = COVER_LIST[i].entity_id;
    api->subscribe_home_assistant_state(eid, optional<std::string>{}, state_cb_t([this, i](const std::string &s) {
      if (this->covers_[i].state != s) { this->covers_[i].state = s; this->cover_changed_(); }
    }));
    api->subscribe_home_assistant_state(eid, optional<std::string>{"current_position"}, state_cb_t([this, i](const std::string &s) {
      uint8_t p = pct_from_ha_(s, 1.0f);
      if (this->covers_[i].position != p) { this->covers_[i].position = p; this->cover_changed_(); }
    }));
  }
}

// ── AC command guard ──────────────────────────────────────────────────────────

std::string RemoteHub::eff_ac_mode_(int i) const {
  const AcGuard &g = this->ac_guard_[i];
  return g.exp_mode ? g.exp_mode_v : this->ac_[i].mode;
}
std::string RemoteHub::eff_ac_fan_(int i) const {
  const AcGuard &g = this->ac_guard_[i];
  return g.exp_fan ? g.exp_fan_v : this->ac_[i].fan;
}
int8_t RemoteHub::eff_ac_temp_(int i) const {
  const AcGuard &g = this->ac_guard_[i];
  return g.exp_temp ? (int8_t) g.exp_temp_v : this->ac_[i].temp;
}

// HA reported a state: a commanded value it now shows is confirmed and stops overriding it.
void RemoteHub::confirm_ac_expected_(int i) {
  AcGuard &g = this->ac_guard_[i];
  if (g.exp_mode && this->ac_[i].mode == g.exp_mode_v) g.exp_mode = false;
  if (g.exp_fan && this->ac_[i].fan == g.exp_fan_v) g.exp_fan = false;
  if (g.exp_temp && this->ac_[i].temp == g.exp_temp_v) g.exp_temp = false;
}

// Sends HA only what differs from the effective state, then pushes the result to the remotes
// whether or not anything changed, so they always get an answer to what they asked for.
void RemoteHub::apply_ac_(int i) {
  AcGuard &g = this->ac_guard_[i];
  g.due = false;
  std::string eid = AC_LIST[i].entity_id;
  uint32_t now = millis();
  bool sent_mode = false;

  if (g.want_mode) {
    g.want_mode = false;
    if (this->eff_ac_mode_(i) != g.want_mode_v) {
      ESP_LOGI(TAG, "AC %d: mode %s -> %s", i, this->eff_ac_mode_(i).c_str(), g.want_mode_v.c_str());
      this->call_homeassistant_service("climate.set_hvac_mode", {{"entity_id", eid}, {"hvac_mode", g.want_mode_v}});
      g.exp_mode = true;
      g.exp_mode_v = g.want_mode_v;
      g.exp_mode_at = now;
      sent_mode = true;
    } else {
      ESP_LOGI(TAG, "AC %d: mode already %s, nothing to send", i, g.want_mode_v.c_str());
    }
  }
  // A unit that was just switched on needs a moment before it takes a fan speed or setpoint.
  if (sent_mode && (g.want_fan || g.want_temp)) {
    g.due = true;
    g.due_at = now + STEP_MS;
    return;
  }
  if (g.want_fan) {
    g.want_fan = false;
    if (this->eff_ac_fan_(i) != g.want_fan_v) {
      ESP_LOGI(TAG, "AC %d: fan %s -> %s", i, this->eff_ac_fan_(i).c_str(), g.want_fan_v.c_str());
      this->call_homeassistant_service("climate.set_fan_mode", {{"entity_id", eid}, {"fan_mode", g.want_fan_v}});
      g.exp_fan = true;
      g.exp_fan_v = g.want_fan_v;
      g.exp_fan_at = now;
    }
  }
  if (g.want_temp) {
    g.want_temp = false;
    if (this->eff_ac_temp_(i) != g.want_temp_v) {
      ESP_LOGI(TAG, "AC %d: temperature %d -> %d", i, (int) this->eff_ac_temp_(i), g.want_temp_v);
      this->call_homeassistant_service("climate.set_temperature",
                                       {{"entity_id", eid}, {"temperature", std::to_string(g.want_temp_v)}});
      g.exp_temp = true;
      g.exp_temp_v = g.want_temp_v;
      g.exp_temp_at = now;
    }
  }
  // Broadcasts are not acknowledged, so also hand the result to the remote that asked (unicast is
  // acknowledged and retried by the radio) and announce it once more a moment later.
  this->mark_dirty_(DIRTY_AC);
  if (this->have_src_) this->send_ac_(this->last_src_);
  g.repeat = true;
  g.repeat_at = now + 2000;
}

// ── sending ───────────────────────────────────────────────────────────────────

void RemoteHub::send_(const uint8_t *dest, const std::vector<uint8_t> &payload) {
  auto *bus = espnow::global_esp_now;
  if (bus == nullptr) return;
  // ESPHome 2026.4 calls the send callback unconditionally when the send report arrives, so a
  // missing one aborts with std::bad_function_call. Always pass one, even if it does nothing.
  esp_err_t err = bus->send(dest, payload, [](esp_err_t) {});
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "send to %s failed: %d", mac_str_(dest).c_str(), (int) err);
  } else {
    this->tx_count_++;
  }
}

// Each STATE packet carries as many records as fit; the record count byte is
// patched after the loop.  Records are built in a scratch writer first so a
// record that does not fit starts a fresh packet instead of being truncated.
void RemoteHub::send_ac_(const uint8_t *dest) {
  rnp::Writer w(this->key_, rnp::STATE_AC);
  w.u8(0);
  uint8_t count = 0;
  for (int i = 0; i < AC_LIST_COUNT; i++) {
    rnp::Writer rec(this->key_, rnp::STATE_AC);
    rec.u8(i).i8(this->eff_ac_temp_(i)).str(this->eff_ac_mode_(i)).str(this->eff_ac_fan_(i));
    size_t rec_len = rec.size() - rnp::HEADER_LEN;
    if (!w.fits(rec_len)) {
      std::vector<uint8_t> out = w.take();
      out[rnp::HEADER_LEN] = count;
      this->send_(dest, out);
      w.begin(this->key_, rnp::STATE_AC);
      w.u8(0);
      count = 0;
    }
    w.u8(i).i8(this->eff_ac_temp_(i)).str(this->eff_ac_mode_(i)).str(this->eff_ac_fan_(i));
    count++;
  }
  std::vector<uint8_t> out = w.take();
  out[rnp::HEADER_LEN] = count;
  this->send_(dest, out);
}

void RemoteHub::send_lights_(const uint8_t *dest) {
  rnp::Writer w(this->key_, rnp::STATE_LIGHTS);
  w.u8(0);
  uint8_t count = 0;
  for (int i = 0; i < LIGHTS_LIST_COUNT; i++) {
    if (!w.fits(3)) {
      std::vector<uint8_t> out = w.take();
      out[rnp::HEADER_LEN] = count;
      this->send_(dest, out);
      w.begin(this->key_, rnp::STATE_LIGHTS);
      w.u8(0);
      count = 0;
    }
    const LightGuard &g = this->light_guard_[i];
    w.u8(i).u8(this->lights_[i].on).u8(g.exp ? g.exp_v : this->lights_[i].brightness);
    count++;
  }
  std::vector<uint8_t> out = w.take();
  out[rnp::HEADER_LEN] = count;
  this->send_(dest, out);
}

void RemoteHub::send_covers_(const uint8_t *dest) {
  rnp::Writer w(this->key_, rnp::STATE_COVERS);
  w.u8(0);
  uint8_t count = 0;
  for (int i = 0; i < COVER_LIST_COUNT; i++) {
    size_t rec_len = 1 + 1 + std::min(this->covers_[i].state.size(), rnp::MAX_STR) + 1;
    if (!w.fits(rec_len)) {
      std::vector<uint8_t> out = w.take();
      out[rnp::HEADER_LEN] = count;
      this->send_(dest, out);
      w.begin(this->key_, rnp::STATE_COVERS);
      w.u8(0);
      count = 0;
    }
    w.u8(i).str(this->covers_[i].state).u8(this->covers_[i].position);
    count++;
  }
  std::vector<uint8_t> out = w.take();
  out[rnp::HEADER_LEN] = count;
  this->send_(dest, out);
}

void RemoteHub::send_quick_(const uint8_t *dest) {
  rnp::Writer w(this->key_, rnp::STATE_QUICK);
  w.u8(0);
  uint8_t count = 0;
  for (int i = 0; i < QUICK_ACTION_COUNT; i++) {
    if (!is_toggle_(QUICK_ACTION_LIST[i].service)) continue;
    if (!w.fits(2)) {
      std::vector<uint8_t> out = w.take();
      out[rnp::HEADER_LEN] = count;
      this->send_(dest, out);
      w.begin(this->key_, rnp::STATE_QUICK);
      w.u8(0);
      count = 0;
    }
    w.u8(i).u8(this->quick_on_[i]);
    count++;
  }
  if (count == 0) return;   // no toggle actions: nothing to tell
  std::vector<uint8_t> out = w.take();
  out[rnp::HEADER_LEN] = count;
  this->send_(dest, out);
}

void RemoteHub::send_snapshot_(const uint8_t *dest) {
  this->send_ac_(dest);
  this->send_lights_(dest);
  this->send_covers_(dest);
  this->send_quick_(dest);
  this->send_(dest, rnp::build_state_done(this->key_));
}

void RemoteHub::broadcast_all() { this->send_snapshot_(rnp::BROADCAST_ADDR); }

// ── screenshot ────────────────────────────────────────────────────────────────

void RemoteHub::request_screenshot() {
  this->shot_pending_ = true;
  this->shot_req_at_ = 0;
  this->shot_mask_ = 0;
  // An awake remote may stay silent for a long time (browsing screens sends nothing), so try
  // now. Unicast is ACKed by the radio: if the remote is asleep the send fails and the request
  // waits for its next HELLO. With no remote seen since boot, broadcast and keep it pending.
  this->send_screenshot_req_(this->have_rx_src_ ? this->last_rx_src_ : rnp::BROADCAST_ADDR);
}

void RemoteHub::maybe_send_screenshot_req_(const uint8_t *src) {
  if (!this->shot_pending_) return;
  if (this->shot_req_at_ != 0 && millis() - this->shot_req_at_ < SCREENSHOT_RETRY_MS) return;
  this->send_screenshot_req_(src);
}

void RemoteHub::send_screenshot_req_(const uint8_t *dest) {
  ESP_LOGI(TAG, "Requesting screenshot from %s", mac_str_(dest).c_str());
  this->shot_mask_ = 0;
  // The espnow component reports the delivery result through the callback, from its own loop.
  bool unicast = memcmp(dest, rnp::BROADCAST_ADDR, 6) != 0;
  auto *bus = espnow::global_esp_now;
  if (bus == nullptr) return;
  this->shot_req_at_ = millis();
  esp_err_t err = bus->send(dest, rnp::build_screenshot_req(this->key_), [this, unicast](esp_err_t result) {
    if (result == ESP_OK || !unicast) return;
    ESP_LOGI(TAG, "Remote is asleep, screenshot will be taken on its next wake");
    this->shot_req_at_ = 0;  // let the next HELLO resend right away
  });
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Screenshot request send failed: %d", (int) err);
    this->shot_req_at_ = 0;
  } else {
    this->tx_count_++;
  }
}

void RemoteHub::handle_screenshot_data_(const uint8_t *src, rnp::Reader &r) {
  uint8_t chunk = 0;
  if (!r.u8(chunk) || chunk >= rnp::SCREENSHOT_CHUNKS) return;
  uint8_t *dst = this->shot_buf_ + chunk * rnp::SCREENSHOT_CHUNK_SIZE;
  for (size_t i = 0; i < rnp::SCREENSHOT_CHUNK_SIZE; i++) {
    if (!r.u8(dst[i])) {
      ESP_LOGW(TAG, "Short screenshot chunk %u from %s", chunk, mac_str_(src).c_str());
      return;
    }
  }
  this->shot_mask_ |= 1 << chunk;
  if (this->shot_mask_ != 0xFF) return;

  // Same format as the WiFi remote's packages/screenshot.yaml, so tools/screenshot.py parses it.
  char line[rnp::SCREENSHOT_CHUNK_SIZE * 2 + 1];
  ESP_LOGI("screenshot", "SCREENSHOT_BEGIN");
  for (size_t c = 0; c < rnp::SCREENSHOT_CHUNKS; c++) {
    for (size_t i = 0; i < rnp::SCREENSHOT_CHUNK_SIZE; i++)
      snprintf(line + i * 2, 3, "%02X", this->shot_buf_[c * rnp::SCREENSHOT_CHUNK_SIZE + i]);
    ESP_LOGI("screenshot", "%s", line);
  }
  ESP_LOGI("screenshot", "SCREENSHOT_END");
  this->shot_pending_ = false;
  this->shot_mask_ = 0;
}

// ── receiving ─────────────────────────────────────────────────────────────────

void RemoteHub::handle_packet(const uint8_t *src, const uint8_t *data, uint8_t size) {
  uint8_t type = rnp::Reader::check(data, size, this->key_);
  if (type == 0) {
    ESP_LOGV(TAG, "Ignoring foreign packet from %s", mac_str_(src).c_str());
    return;
  }
  this->rx_count_++;
  memcpy(this->last_rx_src_, src, 6);
  this->have_rx_src_ = true;
  rnp::Reader r(data, size);

  switch (type) {
    case rnp::HELLO: {
      uint8_t ver = 0, flags = 0;
      r.u8(ver);
      r.u8(flags);
      ESP_LOGI(TAG, "HELLO from %s (proto %u)", mac_str_(src).c_str(), ver);
      if (this->last_remote_sensor_ != nullptr) this->last_remote_sensor_->publish_state(mac_str_(src));
      if (flags & 1) this->send_snapshot_(src);
      this->maybe_send_screenshot_req_(src);
      break;
    }
    case rnp::CMD:
      this->handle_cmd_(src, r);
      this->maybe_send_screenshot_req_(src);
      break;
    case rnp::SCREENSHOT_DATA:
      this->handle_screenshot_data_(src, r);
      break;
    case rnp::BATTERY:
      this->handle_battery_(src, r);
      break;
    default:
      ESP_LOGW(TAG, "Unexpected packet type 0x%02X from %s", type, mac_str_(src).c_str());
      break;
  }
}

void RemoteHub::handle_battery_(const uint8_t *src, rnp::Reader &r) {
  uint8_t pct = 0;
  uint16_t mv = 0;
  if (!r.u8(pct) || !r.u16(mv)) return;
  ESP_LOGI(TAG, "Battery from %s: %u%% (%u mV)", mac_str_(src).c_str(), pct, mv);
  if (this->battery_sensor_ != nullptr) this->battery_sensor_->publish_state(pct);
  if (this->battery_voltage_sensor_ != nullptr) this->battery_voltage_sensor_->publish_state(mv / 1000.0f);
  if (this->last_remote_sensor_ != nullptr) this->last_remote_sensor_->publish_state(mac_str_(src));
}

void RemoteHub::handle_cmd_(const uint8_t *src, rnp::Reader &r) {
  uint8_t domain = 0, idx = 0, action = 0;
  int16_t value = 0;
  if (!r.u8(domain) || !r.u8(idx) || !r.u8(action) || !r.i16(value)) {
    ESP_LOGW(TAG, "Malformed CMD from %s", mac_str_(src).c_str());
    return;
  }
  // Time since the previous command, measured here on the hub, to tell the user's tapping rhythm
  // from the delays in front of the hub (remote debounce, radio, processing).
  static uint32_t last_cmd_ms = 0;
  uint32_t now_ms = millis();
  uint32_t gap_ms = now_ms - last_cmd_ms;
  last_cmd_ms = now_ms;
  ESP_LOGI(TAG, "CMD from %s: domain=%u idx=%u action=%u value=%d (+%u ms)", mac_str_(src).c_str(), domain, idx,
           action, (int) value, (unsigned) gap_ms);

  switch (domain) {
    case rnp::DOM_AC: {
      if (idx >= AC_LIST_COUNT) return;
      const ACEntity &ac = AC_LIST[idx];
      AcGuard &g = this->ac_guard_[idx];
      memcpy(this->last_src_, src, 6);
      this->have_src_ = true;
      // Only record the wish; apply_ac_ talks to HA once the commands stop coming.
      if (action == rnp::AC_SET_MODE) {
        if (value == rnp::AC_MODE_OFF) g.want_mode_v = "off";
        else if (value >= 0 && value < ac.modes_count) g.want_mode_v = ac.modes[value];
        else return;
        g.want_mode = true;
      } else if (action == rnp::AC_SET_FAN) {
        if (value < 0 || value >= ac.fan_modes_count) return;
        g.want_fan_v = ac.fan_modes[value];
        g.want_fan = true;
      } else if (action == rnp::AC_SET_TEMP) {
        if (value < 5 || value > 40) return;
        g.want_temp_v = (int) value;
        g.want_temp = true;
      } else {
        return;
      }
      uint32_t due_at = millis() + SETTLE_MS;
      // Never shorten a wait that is already running: a power press must not cut a temperature
      // sequence short.
      if (!g.due || (int32_t) (due_at - g.due_at) > 0) g.due_at = due_at;
      g.due = true;
      break;
    }

    case rnp::DOM_LIGHTS: {
      if (idx >= LIGHTS_LIST_COUNT) return;
      std::string eid = LIGHTS_LIST[idx].entity_id;
      LightGuard &g = this->light_guard_[idx];
      if (action == rnp::LIGHT_TOGGLE) {
        g.want = false;  // a held brightness must not switch it back on afterwards
        this->call_homeassistant_service("light.toggle", {{"entity_id", eid}});
      } else if (action == rnp::LIGHT_SET_BRIGHTNESS) {
        if (value < 0) value = 0;
        if (value > 100) value = 100;
        // Hold it: every press restarts the wait, the last value goes to HA (loop()).
        g.want = true;
        g.want_v = static_cast<uint8_t>(value);
        g.due_at = millis() + LIGHT_SETTLE_MS;
        g.exp = true;
        g.exp_v = g.want_v;
        g.exp_at = millis();
      } else if (action == rnp::LIGHT_ON) {
        this->call_homeassistant_service("light.turn_on", {{"entity_id", eid}});
      } else if (action == rnp::LIGHT_OFF) {
        g.want = false;
        this->call_homeassistant_service("light.turn_off", {{"entity_id", eid}});
      }
      break;
    }

    case rnp::DOM_COVERS: {
      if (idx >= COVER_LIST_COUNT) return;
      if (this->cover_emu_) {
        this->emulate_cover_(idx, action, value);
        break;
      }
      std::string eid = COVER_LIST[idx].entity_id;
      if (action == rnp::COVER_OPEN) {
        this->call_homeassistant_service("cover.open_cover", {{"entity_id", eid}});
      } else if (action == rnp::COVER_CLOSE) {
        this->call_homeassistant_service("cover.close_cover", {{"entity_id", eid}});
      } else if (action == rnp::COVER_STOP) {
        this->call_homeassistant_service("cover.stop_cover", {{"entity_id", eid}});
      } else if (action == rnp::COVER_SET_POSITION) {
        if (value < 0) value = 0;
        if (value > 100) value = 100;
        this->call_homeassistant_service("cover.set_cover_position",
                                         {{"entity_id", eid}, {"position", std::to_string((int) value)}});
      }
      break;
    }

    case rnp::DOM_AUTOMATIONS: {
      if (idx >= AUTOMATION_LIST_COUNT || action != rnp::AUTO_TRIGGER) return;
      std::string service = AUTOMATION_LIST[idx].service;
      if (service == "scene.turn_on") service = "homeassistant.turn_on";
      this->call_homeassistant_service(service, {{"entity_id", std::string(AUTOMATION_LIST[idx].entity_id)}});
      break;
    }

    case rnp::DOM_QUICK: {
      if (idx >= QUICK_ACTION_COUNT || action != rnp::AUTO_TRIGGER) return;
      std::string service = QUICK_ACTION_LIST[idx].service;
      if (service == "scene.turn_on") service = "homeassistant.turn_on";
      this->call_homeassistant_service(service, {{"entity_id", std::string(QUICK_ACTION_LIST[idx].entity_id)}});
      break;
    }

    default:
      ESP_LOGW(TAG, "Unknown domain %u", domain);
      break;
  }
}

std::string RemoteHub::mac_str_(const uint8_t *mac) {
  char buf[18];
  snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return std::string(buf);
}

}  // namespace remote_hub
}  // namespace esphome
