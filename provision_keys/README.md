# ESP-NOW Key Provisioning

Writes ESP-NOW encryption keys (PMK and LMK) to the NVS flash partition. These keys persist across application reflashes and are read at runtime by the sender and receiver firmware.

## Prerequisites

- ESP-IDF v5.5+ installed and configured
- Target set to your ESP32 variant (e.g., `esp32c3`)

**This project is shared across every board in the system (sender, all receivers), which mix ESP32-C3 and ESP32-S3 chips.** Unlike the sender/receiver projects (each pinned to one chip), this one's `sdkconfig` follows whichever target you last built for. **Before building/flashing, always check (and switch if needed) the target for the board you're about to provision:**

```
Ctrl+Shift+P → ESP-IDF: Set Espressif Device Target → (esp32c3 or esp32s3)
```

Flashing with the wrong target selected will fail or produce a binary for the wrong chip.

## Usage

### Step 1: Generate keys on one device (any board, your choice)

Pick any single board in the system to go first (sender or receiver, doesn't matter). It'll already be fully provisioned once this step is done — no need to flash it again in Step 2.

1. Open `main/provision_keys.c` and set:
   ```c
   #define GENERATE_RANDOM_KEYS 1
   ```

2. Set the target to match the board you picked (see Prerequisites), then build, flash, and open the serial monitor:
   ```bash
   cd provision_keys
   idf.py build flash monitor
   ```

3. The console will print the generated keys:
   ```
   PMK: { 0xA3, 0x1F, 0x9B, 0x44, ... }
   LMK: { 0x7C, 0x02, 0xE8, 0xBB, ... }
   ```

4. Copy these key values.

### Step 2: Flash the same keys to every remaining device

1. Open `main/provision_keys.c` and set:
   ```c
   #define GENERATE_RANDOM_KEYS 0
   ```

2. Paste the copied keys into `FIXED_PMK` and `FIXED_LMK`:
   ```c
   static const uint8_t FIXED_PMK[16] = {
       0xA3, 0x1F, 0x9B, 0x44, ...  // paste your PMK here
   };
   static const uint8_t FIXED_LMK[16] = {
       0x7C, 0x02, 0xE8, 0xBB, ...  // paste your LMK here
   };
   ```

3. Build and flash to each remaining board (switching the target as needed between chip families, see Prerequisites):
   ```bash
   idf.py build flash monitor
   ```

4. Confirm the output shows `Verification: OK`.

### Step 3: Flash the real application

Flash the sender (`person_detection`) and receiver firmware on top. The NVS partition is not overwritten by `idf.py flash`, so the keys survive.

## Erasing keys

To remove the keys from a device:

```bash
ctrl + shift + P 
ESP-IDF:Erase Flash Memory From Device
```

This erases the entire flash including NVS. You will need to re-provision keys afterward.

## Security note

Do not commit this file with your real keys. Either:
- Add `provision_keys/` to `.gitignore`, or
- Replace the key values with placeholders before committing
