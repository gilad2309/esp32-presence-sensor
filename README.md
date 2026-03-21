# ESP32 Wireless Presence Detection System

A wireless room-presence detection system using a 24 GHz mmWave radar sensor and ESP-NOW encrypted communication between ESP32-C3 devices.

## How It Works

```
[DFRobot C4001 Radar] → (UART) → [ESP32-C3 Sender] → (ESP-NOW encrypted) → [ESP32-C3 Receivers] → LED
```

The **sender** reads a mmWave radar sensor that detects human presence (breathing, micro-motion) up to 25m range. On detection change or every 3s heartbeat, it sends an encrypted packet to one or more **receivers**, which indicate presence via LED.

## Project Structure

```
sensor_project/
├── sender/                  # Radar reader + ESP-NOW transmitter
├── receiver/
│   ├── receiver_esp32_c3_devkit_m1/   # RGB LED (color = distance)
│   └── reciver_esp32_c3_mini/         # Simple GPIO LED (on/off)
└── provision_keys/          # Key provisioning tool (flash first)
```

## Hardware

| Component | Model | Role |
|-----------|-------|------|
| Sender board | ESP32-C3 Super Mini | Reads sensor, transmits wirelessly |
| Radar sensor | DFRobot C4001 SEN0609 (24 GHz) | Detects human presence 0.3–25m |
| Receiver board (RGB) | ESP32-C3 DevKit M1 | Shows distance as color (red=close, blue=far) |
| Receiver board (simple) | ESP32-C3 Super Mini | Turns LED on/off |

### Sender Wiring

| C4001 Pin | ESP32-C3 GPIO |
|-----------|---------------|
| TX | GPIO 4 (RX) |
| RX | GPIO 5 (TX) |
| VCC | 3.3V |
| GND | GND |

## Message Format

```c
typedef struct {
    bool  present;   // person detected
    float range;     // distance in meters
    float speed;     // movement speed in m/s
} radar_msg_t;       // 9 bytes, AES-128 encrypted
```

## Security

All communication is encrypted using ESP-NOW with AES-128 CCMP:
- **PMK** (Primary Master Key) — shared across all devices
- **LMK** (Local Master Key) — unique per sender-receiver pair

Keys are stored in NVS (non-volatile storage) and survive firmware updates.

## Getting Started

### Prerequisites
- ESP-IDF v5.5+
- All boards are ESP32-C3 (RISC-V)

### 1. Provision Keys (do this first)

Flash `provision_keys/` to each device to generate and store encryption keys:

1. Flash to the sender with `GENERATE_RANDOM_KEYS = 1` — it generates random keys and prints them
2. Copy the printed keys into `provision_keys.c`
3. Flash to each receiver with `GENERATE_RANDOM_KEYS = 0` — writes the same keys

### 2. Flash Sender

```bash
cd sender
idf.py set-target esp32c3
idf.py build flash monitor
```

### 3. Flash Receiver

```bash
cd receiver/receiver_esp32_c3_devkit_m1   # or reciver_esp32_c3_mini
idf.py set-target esp32c3
idf.py build flash monitor
```

## Configuration

Key parameters in `sender/main/radar_data.c`:

| Parameter | Default | Description |
|-----------|---------|-------------|
| `DETECT_THRESHOLD` | 90 | Sensitivity (0=most sensitive, 9=least) |
| `MIN_RANGE_CM` | 30 | Minimum detection range |
| `MAX_RANGE_CM` | 2500 | Maximum detection range |
| `HEARTBEAT_INTERVAL_MS` | 3000 | Resend interval while present |
| `ABSENT_FRAMES_TO_CLEAR` | 5 | Debounce filter for false negatives |
