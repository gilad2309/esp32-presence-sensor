#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "nvs_flash.h"
#include "radar_data.h"
#include "espnow_sender.h"
#include "deep_sleep.h"
#include <stdio.h>

#define TX_QUEUE_DEPTH 10

void app_main(void)
{
    /* --- NVS (single init point for all components) --- */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    /* --- Wake cause --- */
    esp_sleep_wakeup_cause_t cause = get_wakeup_cause();
    deep_sleep_init();

    switch (cause) {
    case ESP_SLEEP_WAKEUP_GPIO:
        printf("[BOOT] Woke from GPIO — presence detected\n");
        break;
    case ESP_SLEEP_WAKEUP_TIMER:
        printf("[BOOT] Woke from timer (safety fallback)\n");
        if (gpio_get_level(WAKEUP_GPIO) == 0) {
            printf("[BOOT] No presence on GPIO %d — going back to sleep\n", WAKEUP_GPIO);
            enter_deep_sleep();
        }
        printf("[BOOT] Presence detected on GPIO %d — staying awake\n", WAKEUP_GPIO);
        break;
    default:
        printf("[BOOT] First power-on (cause: %d)\n", cause);
        break;
    }

    /* --- Create IPC primitives --- */
    QueueHandle_t tx_queue = xQueueCreate(TX_QUEUE_DEPTH, sizeof(radar_msg_t));
    SemaphoreHandle_t sleep_sem = xSemaphoreCreateBinary();

    /* --- Spawn tasks --- */
    xTaskCreate(radar_reader_task, "radar_rd", 3072, tx_queue, 5, NULL);

    static espnow_task_params_t tx_params;
    tx_params.tx_queue  = tx_queue;
    tx_params.sleep_sem = sleep_sem;
    xTaskCreate(espnow_tx_task, "espnow_tx", 4096, &tx_params, 3, NULL);

    /* --- Send immediate "present" on wake --- */
    if (cause == ESP_SLEEP_WAKEUP_GPIO ||
        (cause == ESP_SLEEP_WAKEUP_TIMER && gpio_get_level(WAKEUP_GPIO) == 1)) {
        radar_msg_t imm = { .present = true, .range = 0.0f, .speed = 0.0f };
        xQueueSend(tx_queue, &imm, 0);
        printf("[BOOT] Queued immediate {present:true}\n");
    }

    /* --- Block until absence confirmed and sent --- */
    xSemaphoreTake(sleep_sem, portMAX_DELAY);
    printf("[BOOT] Absence sent — entering deep sleep\n");
    enter_deep_sleep();
}
