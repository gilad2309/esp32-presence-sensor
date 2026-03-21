# Receivers

Two receiver variants that display presence data from the sender via LED.

| Variant | Board | Output |
|---------|-------|--------|
| [receiver_esp32_c3_devkit_m1](receiver_esp32_c3_devkit_m1/) | ESP32-C3 DevKit M1 | RGB LED — color maps to distance (red=close, blue=far) |
| [reciver_esp32_c3_mini](reciver_esp32_c3_mini/) | ESP32-C3 Super Mini | GPIO LED — on/off presence indicator |

Both load encryption keys from NVS (flash `provision_keys/` first) and listen for ESP-NOW packets from the sender.

If no message is received within 6 seconds, the LED turns off (assumes person left or sender offline).
