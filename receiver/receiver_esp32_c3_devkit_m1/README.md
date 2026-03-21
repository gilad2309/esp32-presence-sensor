# Receiver — ESP32-C3 DevKit M1 (RGB)

Receives encrypted ESP-NOW presence data and maps detection range to an RGB LED color.

## LED Behavior

- **Red** (0°) → person at 0.3m (close)
- **Blue** (240°) → person at 25m (far)
- **Off** → no person detected or no message for 6s

LED is on GPIO 8 (WS2812 addressable LED, driven via RMT peripheral).

## Build & Flash

Requires **ESP-IDF v5.5+**. Flash `provision_keys/` first.

```bash
idf.py set-target esp32c3
idf.py build flash monitor
```

## Dependencies

- `espressif/led_strip` v2.0+ (managed component, auto-downloaded on build)
