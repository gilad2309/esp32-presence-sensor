# Receiver — ESP32-C3 Super Mini (Simple LED)

Receives encrypted ESP-NOW presence data and toggles a GPIO LED on/off.

## LED Behavior

- **On** → person detected
- **Off** → no person detected or no message for 6s

LED is on GPIO 8 (active LOW).

## Build & Flash

Requires **ESP-IDF v5.5+**. Flash `provision_keys/` first.

```bash
idf.py set-target esp32c3
idf.py build flash monitor
```
