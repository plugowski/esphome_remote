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
        ├── oled_remote.yaml                     # Main OLED AC remote configuration
        ├── oled_remote_battery.yaml             # Battery-powered variant
        └── ac_entities.h                        # C++ header for AC entities
```

## Quick Start Guide

### Prerequisites

- ESP32 development board (Lolin D32 or compatible)
- ESPHome installed (`pip install esphome`)
- Home Assistant instance with API access
- OLED display (SSD1306/SH1106 128x64)

### Setting Up a New Device

1. **Copy the main configuration file** to your ESPHome device directory:
   ```bash
   cp devices/oled_remote/oled_remote.yaml /path/to/esphome/your_device_name.yaml
   ```

2. **Edit the substitutions section** at the top of the YAML file. You only need to change:
   - `DEVICE_NAME`: Your unique device name (lowercase, use underscores)
   - `FRIENDLY_NAME`: Human-readable device name
   - `API_KEY`: Your Home Assistant API encryption key

   Example:
   ```yaml
   substitutions:
     BOARD: "esp32dev"                           # Leave unchanged for Lolin board
     DEVICE_NAME: "bedroom-ac-remote"            # Change this
     FRIENDLY_NAME: "Bedroom AC Remote"          # Change this
     API_KEY: "your_api_encryption_key_here"     # Change this
     PIN_WAKE: "0"                               # Leave unchanged for Lolin board
     PIN_I2C_SDA: "27"                           # Leave unchanged for Lolin board
     PIN_I2C_SCL: "25"                           # Leave unchanged for Lolin board
     PIN_BTN_CONTRAST: "32"                      # Leave unchanged for Lolin board
     PIN_BTN_SWITCH_AC: "14"                     # Leave unchanged for Lolin board
     PIN_BTN_TOGGLE: "26"                        # Leave unchanged for Lolin board
     PIN_BTN_FAN: "33"                           # Leave unchanged for Lolin board
     PIN_BTN_MODE: "22"                          # Leave unchanged for Lolin board
     PIN_BTN_TEMP_UP: "23"                       # Leave unchanged for Lolin board
     PIN_BTN_TEMP_DOWN: "19"                     # Leave unchanged for Lolin board
   ```

3. **Configure WiFi credentials** in your ESPHome secrets:
   - Create or edit your `secrets.yaml` file in your ESPHome directory
   - Add your WiFi credentials:
     ```yaml
     wifi_ssid: "YourWiFiName"
     wifi_password: "YourWiFiPassword"
     ```

4. **Update AC entities** (if needed):
   - Edit `ac_entities.h` to match your Home Assistant climate entities
   - The default configuration includes: Living Room, Office, and Bedroom AC units

5. **Compile and upload** to your ESP32:
   ```bash
   esphome run your_device_name.yaml
   ```

### Important Notes

#### Pin Configuration for ESP32 Lolin Board

If you're using the **ESP32 Lolin** or compatible board, **DO NOT change the pin configuration**. The pins are already optimized for this hardware:

- **I2C pins** (SDA: GPIO27, SCL: GPIO25) - for OLED display
- **Button pins** - pre-configured for the physical button layout
- **Wake pin** (GPIO0) - for deep sleep functionality

Only change the pins if you're using a different board or custom hardware layout.

#### What to Customize

**Must customize:**
- `DEVICE_NAME` - Make it unique for each device
- `FRIENDLY_NAME` - Descriptive name for Home Assistant
- `API_KEY` - Your Home Assistant API encryption key

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

### Getting Your API Key

To get your Home Assistant API encryption key:

1. Generate a new key using ESPHome:
   ```bash
   esphome wizard temp.yaml
   ```
   Or manually generate one:
   ```bash
   python3 -c "import secrets; print(secrets.token_hex(32))"
   ```

2. Use this key in your `substitutions` section
3. Add the device to Home Assistant using this same key

## Hardware Setup

### Recommended Components

- **ESP32 Lolin** development board
- **SH1106 128x64 OLED** display (I2C)
- **Push buttons** (7 total for full functionality)
- Optional: Battery for portable operation (see `oled_remote_battery.yaml`)

### Button Functions

1. **Wake/Power** (GPIO0) - Turn on/off the remote
2. **Contrast** (GPIO32) - Adjust display brightness
3. **Switch AC** (GPIO14) - Cycle between different AC units
4. **Toggle** (GPIO26) - Turn AC on/off
5. **Fan** (GPIO33) - Change fan speed
6. **Mode** (GPIO22) - Switch AC mode (cool/fan/dry)
7. **Temp Up** (GPIO23) - Increase temperature
8. **Temp Down** (GPIO19) - Decrease temperature

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
- Review the [Home Assistant Climate integration](https://www.home-assistant.io/integrations/climate/)
- Open an issue in this repository
