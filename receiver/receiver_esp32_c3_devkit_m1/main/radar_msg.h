#pragma once

#include <stdbool.h>

typedef struct {
    bool  present;
    float range;
    float speed;
} __attribute__((packed)) radar_msg_t;
