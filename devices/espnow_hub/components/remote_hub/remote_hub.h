#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "esphome/core/component.h"
#include "esphome/components/api/custom_api_device.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"

// Entity lists shared with the remote (copied into src/ via esphome: includes)
#include "ac_entities.h"
#include "light_entities.h"
#include "cover_entities.h"
#include "automation_entities.h"
#include "quick_action_entities.h"
#include "espnow_protocol.h"

namespace esphome {
namespace remote_hub {

class RemoteHub : public Component, public api::CustomAPIDevice {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_CONNECTION; }

  void set_network_key(uint32_t key) { this->key_ = key; }
  void set_battery_sensor(sensor::Sensor *s) { this->battery_sensor_ = s; }
  void set_battery_voltage_sensor(sensor::Sensor *s) { this->battery_voltage_sensor_ = s; }
  void set_last_remote_sensor(text_sensor::TextSensor *s) { this->last_remote_sensor_ = s; }

  /// Called from the espnow on_receive / on_broadcast triggers.
  void handle_packet(const uint8_t *src, const uint8_t *data, uint8_t size);

  /// Broadcast the full state to every remote that is awake (also wired to a button).
  void broadcast_all();

  /// Ask a remote for its OLED framebuffer (HA "Remote Screenshot" button). Sent right away to
  /// the last remote heard from; if it is asleep (no radio ACK) it goes out on the next HELLO / CMD. The
  /// image is logged as SCREENSHOT_BEGIN / 8 hex lines / SCREENSHOT_END for tools/screenshot.py.
  void request_screenshot();

  uint32_t packets_rx() const { return this->rx_count_; }
  uint32_t packets_tx() const { return this->tx_count_; }

 protected:
  struct AcState {
    std::string mode;
    std::string fan;
    int8_t temp{-1};
  };
  // Guard between the remotes and Home Assistant for one AC unit. Commands only record what the
  // user wants; once they stop arriving for SETTLE_MS the hub sends Home Assistant just the
  // fields that really differ. "exp_*" is what the hub last commanded: until Home Assistant
  // confirms it (or EXPECT_TTL_MS pass) it counts as the unit's state, so a quick ON then OFF
  // is not mistaken for "already off" while HA still reports the old state.
  struct AcGuard {
    bool want_mode{false}, want_fan{false}, want_temp{false};
    std::string want_mode_v, want_fan_v;
    int want_temp_v{0};
    bool exp_mode{false}, exp_fan{false}, exp_temp{false};
    std::string exp_mode_v, exp_fan_v;
    int exp_temp_v{0};
    uint32_t exp_mode_at{0}, exp_fan_at{0}, exp_temp_at{0};
    bool due{false};
    uint32_t due_at{0};
    bool repeat{false};  // announce the result once more shortly after applying it
    uint32_t repeat_at{0};
  };
  struct LightState {
    uint8_t on{rnp::LIGHT_UNKNOWN};
    uint8_t brightness{rnp::VALUE_UNKNOWN};
  };
  struct CoverState {
    std::string state;
    uint8_t position{rnp::VALUE_UNKNOWN};
  };

  void subscribe_all_();
  void mark_dirty_(uint8_t domain_bit);

  void apply_ac_(int i);
  void confirm_ac_expected_(int i);
  std::string eff_ac_mode_(int i) const;
  std::string eff_ac_fan_(int i) const;
  int8_t eff_ac_temp_(int i) const;

  void send_(const uint8_t *dest, const std::vector<uint8_t> &payload);
  void send_ac_(const uint8_t *dest);
  void send_lights_(const uint8_t *dest);
  void send_covers_(const uint8_t *dest);
  void send_snapshot_(const uint8_t *dest);

  void handle_cmd_(const uint8_t *src, rnp::Reader &r);
  void handle_battery_(const uint8_t *src, rnp::Reader &r);
  void handle_screenshot_data_(const uint8_t *src, rnp::Reader &r);
  void maybe_send_screenshot_req_(const uint8_t *src);
  void send_screenshot_req_(const uint8_t *dest);

  static std::string mac_str_(const uint8_t *mac);
  static uint8_t pct_from_ha_(const std::string &v, float scale);

  uint8_t last_src_[6]{0, 0, 0, 0, 0, 0};  // remote that sent the latest AC command
  bool have_src_{false};
  uint8_t last_rx_src_[6]{0, 0, 0, 0, 0, 0};  // remote that sent the latest packet of any kind
  bool have_rx_src_{false};
  bool shot_pending_{false};
  uint32_t shot_req_at_{0};  // 0 = request not sent yet
  uint8_t shot_mask_{0};     // bit n = chunk n received
  uint8_t shot_buf_[rnp::SCREENSHOT_CHUNKS * rnp::SCREENSHOT_CHUNK_SIZE]{};
  uint32_t key_{0};
  sensor::Sensor *battery_sensor_{nullptr};
  sensor::Sensor *battery_voltage_sensor_{nullptr};
  text_sensor::TextSensor *last_remote_sensor_{nullptr};

  AcState ac_[AC_LIST_COUNT > 0 ? AC_LIST_COUNT : 1];
  AcGuard ac_guard_[AC_LIST_COUNT > 0 ? AC_LIST_COUNT : 1];
  LightState lights_[LIGHTS_LIST_COUNT > 0 ? LIGHTS_LIST_COUNT : 1];
  CoverState covers_[COVER_LIST_COUNT > 0 ? COVER_LIST_COUNT : 1];

  static const uint8_t DIRTY_AC = 1, DIRTY_LIGHTS = 2, DIRTY_COVERS = 4;
  uint8_t dirty_{0};
  uint32_t dirty_since_{0};
  uint32_t rx_count_{0};
  uint32_t tx_count_{0};
};

}  // namespace remote_hub
}  // namespace esphome
