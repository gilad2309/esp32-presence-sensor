#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "radar_msg.h"
#include "rgb_led.h"
#include "espnow_receiver.h"

#define PRESENCE_TIMEOUT_MS 6000
#define MIN_RANGE_M         0.30f
#define MAX_RANGE_M         25.00f

static QueueHandle_t led_queue;

static void led_task(void *arg)
{
    radar_msg_t msg;

    for (;;) {
        BaseType_t got = xQueueReceive(led_queue, &msg,
                                       pdMS_TO_TICKS(PRESENCE_TIMEOUT_MS));

        if (got == pdTRUE && msg.present)
            rgb_led_set_range(msg.range);
        else
            rgb_led_off();
    }
}

void app_main(void)
{
    rgb_led_init();

    led_queue = xQueueCreate(1, sizeof(radar_msg_t));
    espnow_receiver_init(led_queue);

    xTaskCreate(led_task, "led_task", 2048, NULL, 5, NULL);

    printf("[ESP-NOW] Receiver ready -- RGB LED maps range to color\n");
    printf("  Red = close (%.0f cm), Blue = far (%.0f cm)\n",
           MIN_RANGE_M * 100, MAX_RANGE_M * 100);
}
