# ESPHome Remote Configuration

This repository contains ESPHome YAML configuration files for remote devices and sensors, specifically designed for AC (Air Conditioning) control using ESP32 boards with OLED displays.

## Overview

ESPHome is a system to control your ESP8266/ESP32 by simple yet powerful configuration files and control them remotely through Home Assistant. This repository serves as a centralized location for managing ESPHome device configurations for AC remote control devices.

## Repository Structure

```
esphome_remote/
├── README.md                                    # This file
└── devices/
    └── oled_remote/
        ├── oled_remote.yaml                     # Basic OLED AC remote
        ├── oled_remote_battery.yaml             # Configuration for PCB v3.1 and newer with fixed voltage divider
        └── ac_entities.h                        # C++ header for AC entities
```

## Quick Start Guide

### Prerequisites

- ESP32 development board (Lolin V1.0.0)
- Home Assistant instance with API access
- OLED display (SH1106 128x64)

### Setting Up a New Device

1. **Copy the main configuration file** to your ESPHome device directory:
2. **Configure WiFi credentials** in your ESPHome secrets:
   - Create or edit your `secrets.yaml` file in your ESPHome directory
   - Add your WiFi credentials:
     ```yaml
     wifi_ssid: "YourWiFiName"
     wifi_password: "YourWiFiPassword"
     ```
3. **Update AC entities** (if needed):
   - Edit `ac_entities.h` to match your Home Assistant climate entities
   - The default configuration includes: Living Room, Office, and Bedroom AC units
4. **Compile and upload** to your ESP32

### Important Notes

#### What to Customize

**Optional customization:**
- WiFi power settings (if signal issues occur)
- Deep sleep duration (default: 3 days)
- Idle timeout (default: 2 minutes)
- Display contrast settings
- AC entity IDs in `ac_entities.h`

**Leave unchanged (for Lolin board):**
- `BOARD` setting
- All `PIN_*` configurations
- I2C frequency and settings
- Display model and configuration

## Hardware Setup

### Recommended Components

- **ESP32 Lolin** development board
- **SH1106 128x64 OLED** display (I2C)
- **Push buttons** (7 total for full functionality)

## Features

- **Multi-AC Control**: Control multiple AC units from one device
- **Deep Sleep**: Battery-efficient operation with automatic sleep
- **OLED Display**: Real-time status display with custom fonts
- **Button Interface**: Physical buttons for all common operations
- **Home Assistant Integration**: Full API integration with encryption
- **Auto-sync**: Automatically syncs with Home Assistant AC states

## Troubleshooting

### Device won't connect to WiFi
- Verify WiFi credentials in `secrets.yaml`
- Check WiFi signal strength
- Try increasing `output_power` in the WiFi section

### Display not working
- Verify I2C pin connections (SDA: GPIO27, SCL: GPIO25)
- Check display model in configuration (SH1106 vs SSD1306)
- Verify display I2C address (default: 0x3C)

### Buttons not responding
- Check pin assignments match your hardware
- Verify pull-up resistor configuration
- Test with a multimeter for button continuity

### API connection fails
- Verify API key matches in both ESPHome and Home Assistant
- Check Home Assistant is accessible from ESP32 network
- Ensure firewall allows API connections

## Contributing

Feel free to submit issues and enhancement requests!

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## Support

For issues and questions:
- Check the [ESPHome documentation](https://esphome.io/)
- [Full project details](https://tech.lugowski.dev/smart-oled-remote-for-home-assistant/)
- 3D Printed case available on [MakerWorld](https://makerworld.com/en/models/1902607-home-assistant-esphome-remote-with-oled-display#profileId-2039332)
- Open an issue in this repository
