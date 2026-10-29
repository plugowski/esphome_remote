#pragma once

// An SSD1306/SH1106 over I2C that survives a missing panel (see display.py), and streams its framebuffer to USB:
//   [I][oledfb:NN]: <frame> <part 0-7> <base64 of 128 bytes>
// in the SH1106 page layout of tools/screenshot.py; the website's oled-preview.js and tools/oled_monitor.py read it.
// The stream runs when `mirror` is on, and always while the display is virtual (no panel answered): a remote without
// an OLED is then still watchable. dump_screenshot() writes the same lines once under the tag `oledshot`.
// preview_line() is the one way these lines leave the chip: through the logger when it is on, otherwise straight to
// the USB-Serial-JTAG port (only while a host is connected), so a build with the logger off - the default - can
// still be watched. Other code that wants to appear in the preview (the battery line) calls it too.

#include <cstring>
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "esphome/components/i2c/i2c.h"
#include "esphome/components/ssd1306_base/ssd1306_base.h"
#ifdef USE_LOGGER
#include "esphome/components/logger/logger.h"
#endif
#ifdef USE_ESP32
#include "soc/soc_caps.h"
#if SOC_USB_SERIAL_JTAG_SUPPORTED
#include "driver/usb_serial_jtag.h"
#define OLED_FAILOVER_USB_DIRECT 1
#endif
#endif

namespace esphome {
namespace oled_failover {

static const char *const TAG = "oled_failover";
static const char *const TAG_FB = "oledfb";      // the live mirror (every changed frame)
static const char *const TAG_SHOT = "oledshot";  // a deliberate screenshot, same lines; the preview files it in its bucket

// One preview line, as `[I][<tag>:0]: <text>`, to whatever is listening on USB. Never blocks.
inline void preview_line(const char *tag, const char *text) {
#ifdef USE_LOGGER
  if (logger::global_logger != nullptr && logger::global_logger->get_baud_rate() > 0) {
    ESP_LOGI(tag, "%s", text);
    return;
  }
#endif
#ifdef OLED_FAILOVER_USB_DIRECT
  if (!usb_serial_jtag_is_driver_installed()) {
    usb_serial_jtag_driver_config_t cfg{};
    cfg.rx_buffer_size = 64;
    cfg.tx_buffer_size = 4096;  // one frame is about 1.5 KB
    if (usb_serial_jtag_driver_install(&cfg) != ESP_OK) return;
  }
  if (!usb_serial_jtag_is_connected()) return;  // nobody on the other end: do not fill the buffer for nothing
  char head[40];
  const int n = snprintf(head, sizeof(head), "[I][%s:0]: ", tag);
  usb_serial_jtag_write_bytes(head, n, 0);
  usb_serial_jtag_write_bytes(text, strlen(text), 0);
  usb_serial_jtag_write_bytes("\r\n", 2, 0);
#else
  (void) tag;
  (void) text;
#endif
}

class OledFailover : public ssd1306_base::SSD1306, public i2c::I2CDevice {
 public:
  void set_mirror(bool mirror) { this->mirror_ = mirror; }
  // True when no panel answered at boot: drawing works, nothing reaches a screen.
  bool is_virtual() const { return this->virtual_; }
  // The frames are streamed (and the battery line printed): the mirror option, or no panel to look at instead.
  bool stream_active() const { return this->mirror_ || this->virtual_; }

  void setup() override {
    this->init_reset_();
    if (this->write(nullptr, 0) != i2c::ERROR_OK) {
      this->virtual_ = true;
      this->init_internal_(this->get_buffer_length_());
      ESP_LOGW(TAG, "No OLED answered on I2C 0x%02X: running with a virtual screen", this->address_);
    } else {
      SSD1306::setup();
    }
    if (!this->stream_active()) this->disable_loop();
  }

  void dump_config() override {
    LOG_DISPLAY("", "OLED (failover)", this);
    ESP_LOGCONFIG(TAG, "  Panel: %s\n  Screen preview (mirror): %s%s", this->virtual_ ? "none, virtual screen" : "connected",
                  YESNO(this->mirror_), this->virtual_ ? " (always on without a panel)" : "");
    LOG_I2C_DEVICE(this);
  }

  // The screenshot commands call this: the current framebuffer as `oledshot` lines.
  void dump_screenshot() {
    if (this->buffer_ == nullptr || this->get_buffer_length_() != FRAME_BYTES) return;
    this->shot_++;
    this->send_frame_(TAG_SHOT, this->shot_);
  }

  void loop() override {
    const size_t len = this->get_buffer_length_();
    if (this->buffer_ == nullptr || len != FRAME_BYTES) return;
    const uint32_t now = millis();
    const bool changed = std::memcmp(this->buffer_, this->last_, len) != 0;
    // Changed frames at most every 150 ms; an unchanged one every 3 s, so a viewer that connects late still gets a picture.
    if (!(changed ? now - this->last_ms_ >= 150 : now - this->last_ms_ >= 3000)) return;
    std::memcpy(this->last_, this->buffer_, len);
    this->last_ms_ = now;
    this->frame_++;
    this->send_frame_(TAG_FB, this->frame_);
  }

 protected:
  void send_frame_(const char *tag, uint32_t number) {
    char line[200];
    for (size_t part = 0; part < FRAME_BYTES / 128; part++) {
      snprintf(line, sizeof(line), "%u %u %s", (unsigned) number, (unsigned) part,
               base64_encode(this->buffer_ + part * 128, 128).c_str());
      preview_line(tag, line);
    }
  }

  static const size_t FRAME_BYTES = 1024;  // 128 x 64

  void command(uint8_t value) override {
    if (!this->virtual_) this->write_byte(0x00, value);
  }

  void write_display_data() override {
    if (this->virtual_) return;
    if (this->is_sh1106_() || this->is_sh1107_()) {
      uint32_t i = 0;
      for (uint8_t page = 0; page < (uint8_t) this->get_height_internal() / 8; page++) {
        this->command(0xB0 + page);                      // row
        this->command(this->is_sh1106_() ? 0x02 : 0x00);  // lower column; 0x02 is the historical SH1106 value
        this->command(0x10);                             // higher column
        for (uint8_t x = 0; x < (uint8_t) this->get_width_internal() / 16; x++) {
          uint8_t data[16];
          for (uint8_t &j : data) j = this->buffer_[i++];
          this->write_bytes(0x40, data, sizeof(data));
        }
      }
    } else {
      size_t block_size = 16;
      if ((this->get_buffer_length_() % 24) == 0) block_size = 24;  // e.g. 72x40, where the buffer is a multiple of 24
      for (uint32_t i = 0; i < this->get_buffer_length_();) {
        uint8_t data[block_size];
        for (uint8_t &j : data) j = this->buffer_[i++];
        this->write_bytes(0x40, data, sizeof(data));
      }
    }
  }

  bool virtual_{false};
  bool mirror_{false};
  uint8_t last_[FRAME_BYTES]{};
  uint32_t last_ms_{0};
  uint32_t frame_{0};
  uint32_t shot_{0};
};

}  // namespace oled_failover
}  // namespace esphome
