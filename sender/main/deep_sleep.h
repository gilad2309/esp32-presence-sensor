#pragma once

#include "driver/gpio.h"
#include "esp_sleep.h"

#define WAKEUP_GPIO       GPIO_NUM_3
#define SAFETY_WAKEUP_SEC 300        // 5-minute fallback

void deep_sleep_init(void);
void enter_deep_sleep(void);
esp_sleep_wakeup_cause_t get_wakeup_cause(void);
