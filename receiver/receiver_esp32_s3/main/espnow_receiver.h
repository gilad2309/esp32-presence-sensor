#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// Initialize WiFi + ESP-NOW and start forwarding messages to the given queue.
// Queue must hold radar_msg_t items (depth 1, use xQueueOverwrite semantics).
void espnow_receiver_init(QueueHandle_t rx_queue);
