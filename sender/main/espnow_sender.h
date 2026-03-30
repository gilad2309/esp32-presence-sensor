#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

typedef struct {
    QueueHandle_t     tx_queue;
    SemaphoreHandle_t sleep_sem;
} espnow_task_params_t;

// ESP-NOW transmit task. Blocks on tx_queue for radar_msg_t messages.
// After sending a present==false message, gives sleep_sem so app_main
// can enter deep sleep.
// param: espnow_task_params_t*
void espnow_tx_task(void *param);
