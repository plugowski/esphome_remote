# OLED Remote (legacy)

This is the original single-purpose AC remote — the first hardware revision using a custom AC control board and a simpler single-file YAML configuration. It is no longer actively developed; the current device is the [Multi-Function Remote](../../README.md).

## Files

| File | Description |
|---|---|
| `oled_remote.yaml` | Base firmware — AC control only |
| `oled_remote_battery.yaml` | Variant with battery level sensor |
| `oled_remote_cover.yaml` | Variant with cover (blind) control |
| `ac_entities.h` | AC unit definitions |
| `cover_entities.h` | Cover entity definitions |
| `display_utils.h` | Display helper functions |
| `generator.html` | Browser-based entity config generator |
| `cover_generator.html` | Browser-based cover config generator |

## Usage

Flash directly with ESPHome:

```bash
esphome run oled_remote.yaml
```

Edit `ac_entities.h` to configure your AC units before flashing.
