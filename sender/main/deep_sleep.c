#include "deep_sleep.h"

#include <stdio.h>
#include "esp_wifi.h"

void deep_sleep_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << WAKEUP_GPIO,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    printf("[SLEEP] GPIO %d configured as input with pull-up (level: %d)\n",
           WAKEUP_GPIO, gpio_get_level(WAKEUP_GPIO));
}

void enter_deep_sleep(void)
{
    printf("[SLEEP] Entering deep sleep — GPIO %d HIGH or %ds timer will wake\n",
           WAKEUP_GPIO, SAFETY_WAKEUP_SEC);

    esp_wifi_stop();

    esp_deep_sleep_enable_gpio_wakeup(1ULL << WAKEUP_GPIO,
                                      ESP_GPIO_WAKEUP_GPIO_HIGH);
    esp_sleep_enable_timer_wakeup((uint64_t)SAFETY_WAKEUP_SEC * 1000000ULL);

    esp_deep_sleep_start();
    // does not return
}

esp_sleep_wakeup_cause_t get_wakeup_cause(void)
{
    return esp_sleep_get_wakeup_cause();
}
