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
    uint8_t netid[8];   // pre-shared pairing gate token -- NOT the PMK

#if GENERATE_RANDOM_KEYS
    printf("Generating random keys...\n");
    esp_fill_random(pmk, 16);
    esp_fill_random(lmk, 16);
    esp_fill_random(netid, 8);
#else
    // Replace with the keys printed by the first device.
    static const uint8_t FIXED_PMK[16] = { 0x91, 0x39, 0xDB, 0xC9, 0x11, 0xB7, 0x47, 0xCC, 0x5F, 0xB1, 0x8A, 0xA5, 0xE0, 0x99, 0xAB, 0x3A};
    static const uint8_t FIXED_LMK[16] = {0x05, 0x78, 0xE7, 0x5B, 0xCE, 0xDA, 0x21, 0x67, 0xC1, 0x36, 0xA9, 0x2C, 0x21, 0x62, 0xE2, 0xB0};
    static const uint8_t FIXED_NETID[8] = { 0x68, 0x3C, 0xA1, 0x75, 0xD3, 0x9F, 0x42, 0x83};  // paste generated value
    printf("Using fixed keys...\n");
    memcpy(pmk, FIXED_PMK, 16);
    memcpy(lmk, FIXED_LMK, 16);
    memcpy(netid, FIXED_NETID, 8);
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
    nvs_set_blob(handle, "netid", netid, 8);
    nvs_commit(handle);
    nvs_close(handle);

    // Print the keys so you can copy them to other devices
    printf("\n=== Keys written to NVS ===\n\n");
    print_key("PMK", pmk, 16);
    print_key("LMK", lmk, 16);
    print_key("NETID", netid, 8);
    printf("\n*** IMPORTANT: Copy these values and flash them to ALL other devices ***\n");
    printf("*** Set GENERATE_RANDOM_KEYS to 0 and paste them into FIXED_PMK/FIXED_LMK/FIXED_NETID ***\n");
    printf("*** Then flash this provisioning app to each remaining device ***\n\n");

    // Verify by reading back
    uint8_t verify_pmk[16];
    uint8_t verify_lmk[16];
    uint8_t verify_netid[8];
    size_t len = 16;

    nvs_open("espnow", NVS_READONLY, &handle);
    nvs_get_blob(handle, "pmk", verify_pmk, &len);
    len = 16;
    nvs_get_blob(handle, "lmk", verify_lmk, &len);
    len = 8;
    nvs_get_blob(handle, "netid", verify_netid, &len);
    nvs_close(handle);

    if (memcmp(pmk, verify_pmk, 16) == 0 && memcmp(lmk, verify_lmk, 16) == 0 &&
        memcmp(netid, verify_netid, 8) == 0)
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
