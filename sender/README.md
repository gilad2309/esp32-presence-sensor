# Sender — Radar Presence Detector

Reads the **DFRobot C4001 24 GHz mmWave radar** via UART and transmits presence data to receivers over **ESP-NOW** with AES-128 encryption.

## How It Works

1. The C4001 radar detects human presence by sensing micro-motion (breathing), not just movement
2. The ESP32-C3 parses UART frames and extracts presence, range, and speed
3. On state change (person arrives/leaves) or every 3s heartbeat, it sends an encrypted packet to the receivers
4. A debounce filter (5 consecutive "absent" frames) prevents false negatives

## Wiring

| C4001 Pin | ESP32-C3 GPIO |
|-----------|---------------|
| TX | GPIO 4 (ESP32 RX) |
| RX | GPIO 5 (ESP32 TX) |
| VCC | 3.3V |
| GND | GND |

## Build & Flash

Requires **ESP-IDF v5.5+**. Make sure to flash `provision_keys/` first to store encryption keys in NVS.

```bash
idf.py set-target esp32c3
idf.py build flash monitor
```

Or via VS Code: `Ctrl+Shift+P` → `ESP-IDF: Build, Flash and start a monitor on your device`

## Configuration

Key parameters in [main/radar_data.c](main/radar_data.c):

| Parameter | Default | Description |
|-----------|---------|-------------|
| `DETECT_THRESHOLD` | 90 | Sensitivity (0=most sensitive, 9=least) |
| `MIN_RANGE_CM` | 30 | Minimum detection range |
| `MAX_RANGE_CM` | 2500 | Maximum detection range (sensor limit 25m) |
| `HEARTBEAT_INTERVAL_MS` | 3000 | Resend interval while person is present |
| `ABSENT_FRAMES_TO_CLEAR` | 5 | Frames before declaring person absent |

Receiver MAC addresses are configured in [main/espnow_sender.c](main/espnow_sender.c).

## Architecture

```
main.c
├── radar_reader_task (priority 5, 3072B stack)
│   └── radar_data.c — UART parser, sensor config
│       └── sends to FreeRTOS queue
└── espnow_tx_task (priority 3, 4096B stack)
    └── espnow_sender.c — encrypted ESP-NOW transmitter
        └── reads from FreeRTOS queue
```
