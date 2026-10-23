#include "remote_hub.h"

#include <algorithm>
#include <functional>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>

#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "esphome/components/api/api_server.h"
#include "esphome/components/espnow/espnow_component.h"

namespace esphome {
namespace remote_hub {

static const char *const TAG = "remote_hub";
static const uint32_t PUSH_DEBOUNCE_MS = 150;
using state_cb_t = std::function<void(const std::string &)>;

// ── lifecycle ─────────────────────────────────────────────────────────────────

void RemoteHub::setup() {
  this->subscribe_all_();
  ESP_LOGI(TAG, "Subscribed to %d AC, %d lights, %d covers; %d automations, %d quick actions",
           AC_LIST_COUNT, LIGHTS_LIST_COUNT, COVER_LIST_COUNT, AUTOMATION_LIST_COUNT, QUICK_ACTION_COUNT);
}

void RemoteHub::loop() {
  if (this->dirty_ == 0) return;
  if (millis() - this->dirty_since_ < PUSH_DEBOUNCE_MS) return;
  uint8_t d = this->dirty_;
  this->dirty_ = 0;
  if (d & DIRTY_AC) this->send_ac_(rnp::BROADCAST_ADDR);
  if (d & DIRTY_LIGHTS) this->send_lights_(rnp::BROADCAST_ADDR);
  if (d & DIRTY_COVERS) this->send_covers_(rnp::BROADCAST_ADDR);
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
    }));
    api->subscribe_home_assistant_state(eid, optional<std::string>{"fan_mode"}, state_cb_t([this, i](const std::string &s) {
      if (this->ac_[i].fan != s) { this->ac_[i].fan = s; this->mark_dirty_(DIRTY_AC); }
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
      }));
    }
  }

  for (int i = 0; i < COVER_LIST_COUNT; i++) {
    std::string eid = COVER_LIST[i].entity_id;
    api->subscribe_home_assistant_state(eid, optional<std::string>{}, state_cb_t([this, i](const std::string &s) {
      if (this->covers_[i].state != s) { this->covers_[i].state = s; this->mark_dirty_(DIRTY_COVERS); }
    }));
    api->subscribe_home_assistant_state(eid, optional<std::string>{"current_position"}, state_cb_t([this, i](const std::string &s) {
      uint8_t p = pct_from_ha_(s, 1.0f);
      if (this->covers_[i].position != p) { this->covers_[i].position = p; this->mark_dirty_(DIRTY_COVERS); }
    }));
  }
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
    rec.u8(i).i8(this->ac_[i].temp).str(this->ac_[i].mode).str(this->ac_[i].fan);
    size_t rec_len = rec.size() - rnp::HEADER_LEN;
    if (!w.fits(rec_len)) {
      std::vector<uint8_t> out = w.take();
      out[rnp::HEADER_LEN] = count;
      this->send_(dest, out);
      w.begin(this->key_, rnp::STATE_AC);
      w.u8(0);
      count = 0;
    }
    w.u8(i).i8(this->ac_[i].temp).str(this->ac_[i].mode).str(this->ac_[i].fan);
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
    w.u8(i).u8(this->lights_[i].on).u8(this->lights_[i].brightness);
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

void RemoteHub::send_snapshot_(const uint8_t *dest) {
  this->send_ac_(dest);
  this->send_lights_(dest);
  this->send_covers_(dest);
  this->send_(dest, rnp::build_state_done(this->key_));
}

void RemoteHub::broadcast_all() { this->send_snapshot_(rnp::BROADCAST_ADDR); }

// ── receiving ─────────────────────────────────────────────────────────────────

void RemoteHub::handle_packet(const uint8_t *src, const uint8_t *data, uint8_t size) {
  uint8_t type = rnp::Reader::check(data, size, this->key_);
  if (type == 0) {
    ESP_LOGV(TAG, "Ignoring foreign packet from %s", mac_str_(src).c_str());
    return;
  }
  this->rx_count_++;
  rnp::Reader r(data, size);

  switch (type) {
    case rnp::HELLO: {
      uint8_t ver = 0, flags = 0;
      r.u8(ver);
      r.u8(flags);
      ESP_LOGI(TAG, "HELLO from %s (proto %u)", mac_str_(src).c_str(), ver);
      if (this->last_remote_sensor_ != nullptr) this->last_remote_sensor_->publish_state(mac_str_(src));
      if (flags & 1) this->send_snapshot_(src);
      break;
    }
    case rnp::CMD:
      this->handle_cmd_(src, r);
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
  ESP_LOGI(TAG, "CMD from %s: domain=%u idx=%u action=%u value=%d", mac_str_(src).c_str(), domain, idx, action,
           (int) value);

  switch (domain) {
    case rnp::DOM_AC: {
      if (idx >= AC_LIST_COUNT) return;
      // Answer every AC command with the full AC frame, even if HA's state ends up unchanged
      // (the unit clamped the value, or it was already set), so the remote always gets an echo.
      this->mark_dirty_(DIRTY_AC);
      const ACEntity &ac = AC_LIST[idx];
      std::string eid = ac.entity_id;
      if (action == rnp::AC_SET_MODE) {
        std::string mode;
        if (value == rnp::AC_MODE_OFF) mode = "off";
        else if (value >= 0 && value < ac.modes_count) mode = ac.modes[value];
        else return;
        this->call_homeassistant_service("climate.set_hvac_mode", {{"entity_id", eid}, {"hvac_mode", mode}});
      } else if (action == rnp::AC_SET_FAN) {
        if (value < 0 || value >= ac.fan_modes_count) return;
        this->call_homeassistant_service("climate.set_fan_mode",
                                         {{"entity_id", eid}, {"fan_mode", std::string(ac.fan_modes[value])}});
      } else if (action == rnp::AC_SET_TEMP) {
        if (value < 5 || value > 40) return;
        this->call_homeassistant_service("climate.set_temperature",
                                         {{"entity_id", eid}, {"temperature", std::to_string((int) value)}});
      }
      break;
    }

    case rnp::DOM_LIGHTS: {
      if (idx >= LIGHTS_LIST_COUNT) return;
      std::string eid = LIGHTS_LIST[idx].entity_id;
      if (action == rnp::LIGHT_TOGGLE) {
        this->call_homeassistant_service("light.toggle", {{"entity_id", eid}});
      } else if (action == rnp::LIGHT_SET_BRIGHTNESS) {
        if (value < 0) value = 0;
        if (value > 100) value = 100;
        this->call_homeassistant_service("light.turn_on",
                                         {{"entity_id", eid}, {"brightness_pct", std::to_string((int) value)}});
      }
      break;
    }

    case rnp::DOM_COVERS: {
      if (idx >= COVER_LIST_COUNT) return;
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
