#include "espnow_sender.h"
#include "radar_data.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_mac.h"
#include <string.h>
#include <stdio.h>

/* ---------- Receiver MAC addresses ---------- */
static const uint8_t receiver_mac[6]            = {0x10, 0x00, 0x3b, 0xcf, 0xc9, 0xe0};
static const uint8_t receiver_devkit_m1[6]      = {0xac, 0xeb, 0xe6, 0x8a, 0xfa, 0x04};

/* ---------- Encryption keys ---------- */
static uint8_t PMK[16];
static uint8_t LMK[16];

/* ---------- Send-confirm semaphore ---------- */
static SemaphoreHandle_t s_send_done;

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

static void on_sent(const wifi_tx_info_t *tx_info, esp_now_send_status_t status)
{
    if (status != ESP_NOW_SEND_SUCCESS) {
        printf("[ESP-NOW] Send failed\n");
    }
    xSemaphoreGive(s_send_done);
}

static void espnow_sender_init(void)
{
    /* NVS is already initialized in app_main — just load keys */
    if (!load_keys_from_nvs()) {
        printf("[ESP-NOW] FATAL: Cannot start without keys\n");
        vTaskDelete(NULL);
        return;
    }

    esp_netif_init();
    esp_event_loop_create_default();

    wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&wifi_cfg);
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();
    esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_LR);
    esp_wifi_set_max_tx_power(84); // 21 dBm (max)

    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    printf("[ESP-NOW] Sender MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    esp_now_init();
    esp_now_set_pmk(PMK);
    esp_now_register_send_cb(on_sent);

    esp_now_peer_info_t peer = {0};
    memcpy(peer.lmk, LMK, 16);
    peer.channel = 0;
    peer.encrypt = true;

    memcpy(peer.peer_addr, receiver_mac, 6);
    esp_now_add_peer(&peer);

    memcpy(peer.peer_addr, receiver_devkit_m1, 6);
    esp_now_add_peer(&peer);

    /* Configure Long Range (LR) PHY rate for both peers */
    esp_now_rate_config_t lr_rate = {
        .phymode = WIFI_PHY_MODE_LR,
        .rate    = WIFI_PHY_RATE_LORA_250K,
        .ersu    = false,
        .dcm     = false,
    };
    esp_now_set_peer_rate_config(receiver_mac, &lr_rate);
    esp_now_set_peer_rate_config(receiver_devkit_m1, &lr_rate);

    printf("[ESP-NOW] Sender initialized — 2 receivers (unicast + encrypted + LR)\n");
}

void espnow_tx_task(void *param)
{
    espnow_task_params_t *params = (espnow_task_params_t *)param;
    QueueHandle_t     tx_queue  = params->tx_queue;
    SemaphoreHandle_t sleep_sem = params->sleep_sem;

    s_send_done = xSemaphoreCreateBinary();

    espnow_sender_init();

    radar_msg_t msg;
    while (1) {
        xQueueReceive(tx_queue, &msg, portMAX_DELAY);

        esp_now_send(receiver_mac, (uint8_t *)&msg, sizeof(msg));

        /* Wait for on_sent callback (1s timeout to prevent hang) */
        if (xSemaphoreTake(s_send_done, pdMS_TO_TICKS(1000)) != pdTRUE) {
            printf("[ESP-NOW] Send confirm timeout\n");
        }

        printf("[ESP-NOW] Sent → present:%d range:%.2fm speed:%.2fm/s\n",
               msg.present, msg.range, msg.speed);

        /* If we just confirmed absence was sent, signal app_main to sleep */
        if (!msg.present) {
            printf("[ESP-NOW] Absence sent — giving sleep semaphore\n");
            xSemaphoreGive(sleep_sem);
        }
    }
}
