#include "rgb_led.h"
#include "led_strip.h"

#define LED_GPIO        48      // ESP32-S3-DevKitC-1: onboard RGB LED
#define LED_BRIGHTNESS  25      // 0-255, keep low -- very bright at full scale

static led_strip_handle_t led_strip;

/* ---------- Public API ---------- */

void rgb_led_init(void)
{
    led_strip_config_t strip_cfg = {
        .strip_gpio_num = LED_GPIO,
        .max_leds = 1,
        .led_pixel_format = LED_PIXEL_FORMAT_GRB,
        // This board's onboard LED is SK6812, not true WS2812 -- wrong
        // timing here causes bit misreads (wrong/flickering color).
        .led_model = LED_MODEL_SK6812,
    };
    led_strip_rmt_config_t rmt_cfg = {
        .resolution_hz = 10 * 1000 * 1000,  // 10 MHz
    };
    led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &led_strip);
    led_strip_clear(led_strip);
}

void rgb_led_on(void)
{
    led_strip_set_pixel(led_strip, 0, 0, 0, LED_BRIGHTNESS);  // solid blue
    led_strip_refresh(led_strip);
}

void rgb_led_off(void)
{
    led_strip_clear(led_strip);
}
