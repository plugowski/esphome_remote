# ESPHome Remote

ESP32-based wireless remote with a 128×64 OLED display, controlled via ESPHome and Home Assistant. Supports AC units, lights, covers, automations, and display settings — all navigable from a physical button pad.

- Full project write-up: [https://tech.lugowski.dev/guides/smart-oled-remote/](https://tech.lugowski.dev/guides/smart-oled-remote/)
- 3D-printed case: [MakerWorld](https://makerworld.com/en/models/1902607-home-assistant-esphome-remote-with-oled-display#profileId-2039332)
- ESPHome docs: [esphome.io](https://esphome.io/)

---

## Repository structure

```
esphome_remote/
├── README.md                   ← this file
├── secrets.yaml                ← shared WiFi credentials
└── devices/
    ├── multi_function_remote/  ← current device (v2 hardware, 6-button pad)
    │   ├── README.md           ← setup and configuration guide
    │   └── …
    └── oled_remote/            ← legacy device (v1 hardware, original board)
        └── …
```

→ For setup instructions, entity configuration, and build commands see
**[devices/multi_function_remote/README.md](devices/multi_function_remote/README.md)**.

---

## Hardware

| Component | Spec |
|---|---|
| MCU | ESP32 (esp32dev) |
| Display | SH1106 128×64 OLED, I2C |
| I2C pins | SDA GPIO27 · SCL GPIO25 |
| Button pad | 6-button directional pad (v2) or original layout (v1) |
| Power | LiPo battery with voltage divider on GPIO34 |

---

## Troubleshooting

**Device won't connect to WiFi**
- Verify credentials in `secrets.yaml`
- Try increasing `output_power` in `remote.yaml`

**Display not working**
- Check SDA/SCL wiring (GPIO27/GPIO25)
- Verify the display model (`SH1106` vs `SSD1306`) and I2C address (`0x3C`)

**Buttons not responding**
- Check pin assignments match your hardware revision (v1 vs v2 layout)
- Pins use internal pull-ups — button should connect GPIO to GND

**API connection fails after flashing**
- Make sure the `API_KEY` in `remote.yaml` matches what you entered in HA
- HA must be reachable from the ESP32 subnet
