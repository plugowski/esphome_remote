#pragma once
#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome {
namespace boot_gate {

// Setup stops at this component while it is held (see __init__.py). hold()
// has to be called before setup reaches the gate, i.e. from an on_boot with a
// higher priority than the gate's setup_priority.
class BootGate : public Component {
 public:
  void set_timeout_ms(uint32_t ms) { this->timeout_ms_ = ms; }

  void hold() {
    this->held_ = true;
    this->held_since_ = millis();
  }
  void release() { this->held_ = false; }
  bool is_held() const { return this->held_; }

  bool can_proceed() override {
    if (this->held_ && millis() - this->held_since_ > this->timeout_ms_) {
      ESP_LOGW("boot_gate", "Held for %u ms without the device sleeping, continuing setup",
               (unsigned) this->timeout_ms_);
      this->held_ = false;
    }
    return !this->held_;
  }

  // Ordered by the YAML setup_priority; this default only applies without one.
  float get_setup_priority() const override { return setup_priority::LATE - 150.0f; }

 protected:
  bool held_{false};
  uint32_t held_since_{0};
  uint32_t timeout_ms_{30000};
};

}  // namespace boot_gate
}  // namespace esphome
