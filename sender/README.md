# ESP32 Person Detection

A room-presence detection system built on the **ESP32-C3 Super Mini** and the **DFRobot C4001 24 GHz mmWave radar sensor**.
Each sensor node reads the radar and sends detection events wirelessly to a central hub using **ESP-NOW** with AES-128 encryption.

---

## How it works

```
[DFRobot C4001] --UART--> [ESP32-C3 (sensor node)] --ESP-NOW (encrypted)--> [ESP32 hub]
```

- The C4001 runs in **existence mode** — it detects human presence by sensing micro-motion from breathing, not just movement.
- The ESP32-C3 parses the radar frames and sends a compact message (`present`, `range`) to the hub over ESP-NOW.
- All traffic is **AES-128 encrypted** using a PMK + per-device LMK key pair.

---

## Wiring

| C4001 pin | ESP32-C3 GPIO |
|-----------|--------------|
| TX        | GPIO 4 (ESP32 RX) |
| RX        | GPIO 5 (ESP32 TX) |
| VCC       | 3.3 V |
| GND       | GND |

---

## Build, Flash & Monitor

Requires the **ESP-IDF extension** for VS Code.

1. Open the project folder in VS Code
2. Press `Ctrl + Shift + P` to open the command palette
3. Run each step in order:
   - `ESP-IDF: Build your project`
   - `ESP-IDF: Flash your project`
   - `ESP-IDF: Monitor your device`

   Or run all three at once with:
   - `ESP-IDF: Build, Flash and start a monitor on your device`

---

## Configuration

Key settings are at the top of [main/radar_data.c](main/radar_data.c):

```c
#define TRIG_SENSITIVITY 5   // 0 (least sensitive) to 9 (most sensitive)
#define KEEP_SENSITIVITY 5   // sensitivity to keep an ongoing detection
```

The receiver MAC address and encryption keys are in [main/espnow_sender.c](main/espnow_sender.c).

---

## Encryption keys (PMK and LMK)

### Why they are needed

ESP-NOW packets are sent over the air and can be captured by anyone nearby with a Wi-Fi adapter.
Without encryption, the presence/absence data from every room would be readable by anyone.

- **PMK (Primary Master Key)** — a shared secret for the whole network. Every device (all sensors + the hub) must have the same PMK.
- **LMK (Local Master Key)** — a per-pair secret between one sensor and the hub. Each sensor gets its own unique LMK. If one device is stolen, only that device's LMK is compromised — the rest of the network stays secure.

Together they provide **AES-128 (CCMP)** encryption on every packet.

### How to generate keys

A PowerShell script is included to generate cryptographically random keys.

**Run from the project folder:**

```powershell
powershell -ExecutionPolicy Bypass -File scripts/generate_keys.ps1
```

For a different number of sensors (default is 5):

```powershell
powershell -ExecutionPolicy Bypass -File scripts/generate_keys.ps1 -NumSensors 3
```

The script writes ready-to-paste C arrays to **`scripts/keys.txt`** and also prints them to the terminal.

### Where to paste the keys

| Key | Paste into |
|-----|-----------|
| `PMK` | Every sensor's `espnow_sender.c` **and** the hub's receiver file |
| `LMK_SENSOR1` | Sensor 1's `espnow_sender.c` **and** the hub (as the LMK for sensor 1's peer entry) |
| `LMK_SENSOR2` | Sensor 2's `espnow_sender.c` **and** the hub (as the LMK for sensor 2's peer entry) |
| ... | ... |

> **Important:** Generate new keys for every deployment. Never reuse keys across different installations or share them publicly.
