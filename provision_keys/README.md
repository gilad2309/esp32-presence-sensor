# ESP-NOW Key Provisioning

Writes ESP-NOW encryption keys (PMK and LMK) to the NVS flash partition. These keys persist across application reflashes and are read at runtime by the sender and receiver firmware.

## Prerequisites

- ESP-IDF v5.5+ installed and configured
- Target set to your ESP32 variant (e.g., `esp32c3`)

## Usage

### Step 1: Generate keys on the first device (sender)

1. Open `main/provision_keys.c` and set:
   ```c
   #define GENERATE_RANDOM_KEYS 1
   ```

2. Build, flash, and open the serial monitor:
   ```bash
   cd provision_keys
   idf.py set-target esp32c3
   idf.py build flash monitor
   ```

3. The console will print the generated keys:
   ```
   PMK: { 0xA3, 0x1F, 0x9B, 0x44, ... }
   LMK: { 0x7C, 0x02, 0xE8, 0xBB, ... }
   ```

4. Copy these key values.

### Step 2: Flash the same keys to all other devices (receivers)

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

3. Build and flash to each receiver:
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
