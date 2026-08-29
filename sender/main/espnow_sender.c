#include "espnow_sender.h"
#include "radar_data.h"
#include "espnow_pairing.h"
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

/* ---------- Encryption keys + pairing token ---------- */
static uint8_t PMK[16];
static uint8_t LMK[16];
static uint8_t NETID[ESPNOW_PAIRING_NETID_LEN];

/* ---------- Discovered receiver ---------- */
static uint8_t g_receiver_mac[6];

// How long each discovery burst waits for a reply before trying again.
// wait_for_receiver() loops this indefinitely -- the sender stays awake
// (not sleeping between attempts) until a receiver actually answers, no
// upper bound. Trade-off: if a receiver is genuinely unreachable for a
// long stretch, this burns battery instead of backing off to the safety
// timer, unlike a single bounded attempt would.
#define DISCOVERY_BURST_MS 10000

/* ---------- Send-confirm semaphore ---------- */
static SemaphoreHandle_t s_send_done;
static volatile bool s_last_send_ok = false;

/* ---------- Self-healing re-pairing ---------- */
#define FORGET_THRESHOLD 8
static int s_consecutive_failures = 0;

static bool load_keys_from_nvs(void)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open("espnow", NVS_READONLY, &handle);
    if (ret != ESP_OK)
    {
        printf("[ESP-NOW] NVS open failed: %s — did you run provision_keys?\n", esp_err_to_name(ret));
        return false;
    }

    size_t len = 16;
    ret = nvs_get_blob(handle, "pmk", PMK, &len);
    if (ret != ESP_OK)
    {
        printf("[ESP-NOW] PMK not found in NVS: %s\n", esp_err_to_name(ret));
        nvs_close(handle);
        return false;
    }

    len = 16;
    ret = nvs_get_blob(handle, "lmk", LMK, &len);
    if (ret != ESP_OK)
    {
        printf("[ESP-NOW] LMK not found in NVS: %s\n", esp_err_to_name(ret));
        nvs_close(handle);
        return false;
    }

    len = ESPNOW_PAIRING_NETID_LEN;
    ret = nvs_get_blob(handle, "netid", NETID, &len);
    if (ret != ESP_OK)
    {
        printf("[ESP-NOW] NETID not found in NVS: %s (re-run provision_keys)\n", esp_err_to_name(ret));
        nvs_close(handle);
        return false;
    }

    nvs_close(handle);
    printf("[ESP-NOW] Keys loaded from NVS\n");
    return true;
}

static void on_sent(const wifi_tx_info_t *tx_info, esp_now_send_status_t status)
{
    s_last_send_ok = (status == ESP_NOW_SEND_SUCCESS);
    if (!s_last_send_ok)
    {
        printf("[ESP-NOW] Send failed\n");
    }
    xSemaphoreGive(s_send_done);
}

static bool send_once(const uint8_t mac[6], const void *data, size_t len)
{
    esp_now_send(mac, (const uint8_t *)data, len);
    return xSemaphoreTake(s_send_done, pdMS_TO_TICKS(1000)) == pdTRUE && s_last_send_ok;
}

// Blocks until a receiver is found, retrying in DISCOVERY_BURST_MS bursts
// indefinitely -- no upper bound, no sleep in between (see the trade-off
// note on DISCOVERY_BURST_MS above). Fills g_receiver_mac on return.
static void wait_for_receiver(void)
{
    int attempt = 0;
    while (!espnow_pairing_get_receiver(DISCOVERY_BURST_MS, g_receiver_mac))
    {
        attempt++;
        printf("[ESP-NOW] No receiver found (attempt %d, %ds each) -- retrying\n",
               attempt, DISCOVERY_BURST_MS / 1000);
    }
}

static void espnow_sender_init(void)
{
    /* NVS is already initialized in app_main — just load keys */
    if (!load_keys_from_nvs())
    {
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

    espnow_pairing_init(NETID, LMK, NULL);   // sender has no app-level recv

    wait_for_receiver();   // blocks until paired, see DISCOVERY_BURST_MS note
}

void espnow_tx_task(void *param)
{
    espnow_task_params_t *params = (espnow_task_params_t *)param;
    QueueHandle_t tx_queue = params->tx_queue;
    SemaphoreHandle_t sleep_sem = params->sleep_sem;

    s_send_done = xSemaphoreCreateBinary();

    espnow_sender_init();   // blocks inside until paired, see wait_for_receiver()

    radar_msg_t msg;
    while (1)
    {
        xQueueReceive(tx_queue, &msg, portMAX_DELAY);

        bool ok = send_once(g_receiver_mac, &msg, sizeof(msg));

        printf("[ESP-NOW] %s -> present:%d range:%.2fm speed:%.2fm/s\n",
               ok ? "Sent" : "Send FAILED", msg.present, msg.range, msg.speed);

        if (!ok)
        {
            if (++s_consecutive_failures >= FORGET_THRESHOLD)
            {
                printf("[ESP-NOW] %d consecutive failures -- forgetting receiver and re-discovering\n",
                       s_consecutive_failures);
                espnow_pairing_forget_receiver();
                wait_for_receiver();   // blocks until re-paired, same as at boot
                s_consecutive_failures = 0;
            }
        }
        else
        {
            s_consecutive_failures = 0;
        }

        /* If we just confirmed absence was sent, signal app_main to sleep */
        if (!msg.present)
        {
            printf("[ESP-NOW] Absence sent - giving sleep semaphore\n");
            xSemaphoreGive(sleep_sem);
        }
    }
}
