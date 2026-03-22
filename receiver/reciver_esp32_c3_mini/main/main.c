#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "nvs_flash.h"

/* ---------- LED configuration ---------- */
#define LED_GPIO    8   // onboard LED on ESP32-C3 Super Mini
#define LED_ON      0   // active LOW
#define LED_OFF     1

/* ---------- Timing ---------- */
// If no heartbeat arrives within this time, assume person left.
// Should be > sender's HEARTBEAT_INTERVAL_MS (3000ms).
#define PRESENCE_TIMEOUT_MS 6000

/* ---------- Pairing ---------- */
static const uint8_t sender_mac[6] = {0x10, 0x00, 0x3b, 0xd1, 0xe0, 0xf4};
static uint8_t PMK[16];
static uint8_t LMK[16];

static bool load_keys_from_nvs(void)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open("espnow", NVS_READONLY, &handle);
    if (ret != ESP_OK) {
        printf("[ESP-NOW] NVS open failed: %s — did you run provision_keys?\n", esp_err_to_name(ret));
        return false;
    }

    size_t len = 16;
    ret = nvs_get_blob(handle, "pmk", PMK, &len);
    if (ret != ESP_OK) {
        printf("[ESP-NOW] PMK not found in NVS: %s\n", esp_err_to_name(ret));
        nvs_close(handle);
        return false;
    }

    len = 16;
    ret = nvs_get_blob(handle, "lmk", LMK, &len);
    if (ret != ESP_OK) {
        printf("[ESP-NOW] LMK not found in NVS: %s\n", esp_err_to_name(ret));
        nvs_close(handle);
        return false;
    }

    nvs_close(handle);
    printf("[ESP-NOW] Keys loaded from NVS\n");
    return true;
}

/* ---------- Shared message type (must match radar_data.h) ---------- */
typedef struct {
    bool  present;
    float range;
    float speed;
} __attribute__((packed)) radar_msg_t;

/* ---------- LED task ---------- */

static TaskHandle_t led_task_handle = NULL;

#define NOTIFY_PRESENT 1
#define NOTIFY_ABSENT  2

static void led_task(void *arg)
{
    for (;;) {
        uint32_t value = 0;
        xTaskNotifyWait(0, ULONG_MAX, &value,
                        pdMS_TO_TICKS(PRESENCE_TIMEOUT_MS));

        if (value == NOTIFY_PRESENT)
            gpio_set_level(LED_GPIO, LED_ON);
        else
            gpio_set_level(LED_GPIO, LED_OFF);
    }
}

/* ---------- ESP-NOW receive callback ---------- */

static void on_receive(const esp_now_recv_info_t *info,
                       const uint8_t *data, int len)
{
    if (len != sizeof(radar_msg_t))
        return;

    radar_msg_t msg;
    memcpy(&msg, data, sizeof(msg));

    if (msg.present) {
        printf("[RX] Person DETECTED — range: %.2f m | speed: %.2f m/s | RSSI: %d dBm\n",
               msg.range, msg.speed, info->rx_ctrl->rssi);
        xTaskNotify(led_task_handle, NOTIFY_PRESENT, eSetValueWithOverwrite);
    } else {
        printf("[RX] Person LEFT | RSSI: %d dBm\n", info->rx_ctrl->rssi);
        xTaskNotify(led_task_handle, NOTIFY_ABSENT, eSetValueWithOverwrite);
    }
}

/* ---------- Initialization ---------- */

static void init_led(void)
{
    gpio_reset_pin(LED_GPIO);
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(LED_GPIO, LED_OFF);
}

static void init_wifi(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    if (!load_keys_from_nvs()) {
        printf("[ESP-NOW] FATAL: Cannot start without keys\n");
        return;
    }

    esp_netif_init();
    esp_event_loop_create_default();

    wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&wifi_cfg);
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();
    esp_wifi_set_max_tx_power(84);  // 21 dBm (max)

    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    printf("[ESP-NOW] Receiver MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static void init_espnow(void)
{
    esp_now_init();
    esp_now_set_pmk(PMK);
    esp_now_register_recv_cb(on_receive);

    esp_now_peer_info_t peer = {0};
    memcpy(peer.peer_addr, sender_mac, 6);
    memcpy(peer.lmk, LMK, 16);
    peer.channel = 0;
    peer.encrypt = true;
    esp_now_add_peer(&peer);
}

/* ---------- Entry point ---------- */

void app_main(void)
{
    init_led();
    init_wifi();
    init_espnow();

    xTaskCreate(led_task, "led_task", 2048, NULL, 5, &led_task_handle);

    printf("[ESP-NOW] Receiver ready — waiting for detections...\n");
}
