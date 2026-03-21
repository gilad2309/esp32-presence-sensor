#pragma once

#include <stdbool.h>

typedef struct {
    bool  present;
    float range;   // meters
    float speed;   // m/s
} __attribute__((packed)) radar_msg_t;

// Reads and parses C4001 SEN0609 UART frames, pushes radar_msg_t to a queue.
// param: QueueHandle_t
void radar_reader_task(void *param);
