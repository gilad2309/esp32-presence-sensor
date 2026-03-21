#include "nvs_flash.h"
#include "esp_random.h"
#include <stdio.h>
#include <string.h>

// Set to 1 to generate new random keys.
// Set to 0 to write the specific keys defined below.
#define GENERATE_RANDOM_KEYS 0

static void print_key(const char *name, const uint8_t *key, size_t len)
{
    printf("%s: { ", name);
    for (size_t i = 0; i < len; i++)
    {
        printf("0x%02X%s", key[i], (i < len - 1) ? ", " : "");
    }
    printf(" }\n");
}

void app_main(void)
{
    printf("\n=== ESP-NOW Key Provisioning ===\n\n");

    // Init NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        printf("Erasing NVS partition...\n");
        nvs_flash_erase();
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK)
    {
        printf("NVS init failed: %s\n", esp_err_to_name(ret));
        return;
    }

    // Prepare keys
    uint8_t pmk[16];
    uint8_t lmk[16];

#if GENERATE_RANDOM_KEYS
    printf("Generating random keys...\n");
    esp_fill_random(pmk, 16);
    esp_fill_random(lmk, 16);
#else
    // Replace with the keys printed by the first device.
    static const uint8_t FIXED_PMK[16] = {0xE5, 0xCB, 0xB3, 0xB7, 0x87, 0x20, 0x00, 0xF5, 0xA7, 0x58, 0x6C, 0xC7, 0x44, 0xC5, 0x5B, 0x91};
    static const uint8_t FIXED_LMK[16] = {0x3A, 0xCB, 0x5E, 0x99, 0xCF, 0xDD, 0x5B, 0x40, 0xF7, 0x30, 0x12, 0xC2, 0x64, 0x13, 0x7A, 0x27};
    printf("Using fixed keys...\n");
    memcpy(pmk, FIXED_PMK, 16);
    memcpy(lmk, FIXED_LMK, 16);
#endif

    // Write to NVS
    nvs_handle_t handle;
    ret = nvs_open("espnow", NVS_READWRITE, &handle);
    if (ret != ESP_OK)
    {
        printf("nvs_open failed: %s\n", esp_err_to_name(ret));
        return;
    }

    nvs_set_blob(handle, "pmk", pmk, 16);
    nvs_set_blob(handle, "lmk", lmk, 16);
    nvs_commit(handle);
    nvs_close(handle);

    // Print the keys so you can copy them to other devices
    printf("\n=== Keys written to NVS ===\n\n");
    print_key("PMK", pmk, 16);
    print_key("LMK", lmk, 16);
    printf("\n*** IMPORTANT: Copy these keys and flash them to ALL receivers ***\n");
    printf("*** Set GENERATE_RANDOM_KEYS to 0 and paste the keys into FIXED_PMK/FIXED_LMK ***\n");
    printf("*** Then flash this provisioning app to each receiver ***\n\n");

    // Verify by reading back
    uint8_t verify_pmk[16];
    uint8_t verify_lmk[16];
    size_t len = 16;

    nvs_open("espnow", NVS_READONLY, &handle);
    nvs_get_blob(handle, "pmk", verify_pmk, &len);
    len = 16;
    nvs_get_blob(handle, "lmk", verify_lmk, &len);
    nvs_close(handle);

    if (memcmp(pmk, verify_pmk, 16) == 0 && memcmp(lmk, verify_lmk, 16) == 0)
    {
        printf("Verification: OK — keys read back correctly\n");
    }
    else
    {
        printf("Verification: FAILED — keys do not match!\n");
    }

    printf("\nProvisioning complete. You can now flash your real application.\n");
    printf("The NVS partition will keep these keys.\n");
}
