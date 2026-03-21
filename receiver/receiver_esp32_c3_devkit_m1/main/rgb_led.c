#include "rgb_led.h"
#include "led_strip.h"

#define LED_GPIO        8
#define LED_BRIGHTNESS  25      // 0-255, keep low -- WS2812 is very bright

#define MIN_RANGE_M     0.30f   // sensor minimum (30 cm)
#define MAX_RANGE_M     25.00f  // sensor maximum (2500 cm)

static led_strip_handle_t led_strip;

// --- HSV-to-RGB conversion ---------------------------------------------------
// hue: 0-360, sat/val: 0-255.  Output: r,g,b each 0-255.
static void hsv_to_rgb(uint16_t hue, uint8_t sat, uint8_t val,
                       uint8_t *r, uint8_t *g, uint8_t *b)
{
    uint8_t region = hue / 60;
    uint16_t remainder = (hue % 60) * 255 / 60;

    uint8_t p = (uint16_t)val * (255 - sat) / 255;
    uint8_t q = (uint16_t)val * (255 - (uint16_t)sat * remainder / 255) / 255;
    uint8_t t = (uint16_t)val * (255 - (uint16_t)sat * (255 - remainder) / 255) / 255;

    switch (region) {
    case 0:  *r = val; *g = t;   *b = p;   break;
    case 1:  *r = q;   *g = val; *b = p;   break;
    case 2:  *r = p;   *g = val; *b = t;   break;
    case 3:  *r = p;   *g = q;   *b = val; break;
    case 4:  *r = t;   *g = p;   *b = val; break;
    default: *r = val; *g = p;   *b = q;   break;
    }
}

// --- Range-to-color mapping --------------------------------------------------
// Maps range to HSV hue: close = red (0), far = blue (240).
static void range_to_rgb(float range_m, uint8_t *r, uint8_t *g, uint8_t *b)
{
    if (range_m < MIN_RANGE_M) range_m = MIN_RANGE_M;
    if (range_m > MAX_RANGE_M) range_m = MAX_RANGE_M;

    float ratio = (range_m - MIN_RANGE_M) / (MAX_RANGE_M - MIN_RANGE_M);
    uint16_t hue = (uint16_t)(ratio * 240.0f);

    hsv_to_rgb(hue, 255, LED_BRIGHTNESS, r, g, b);
}

// --- Public API --------------------------------------------------------------

void rgb_led_init(void)
{
    led_strip_config_t strip_cfg = {
        .strip_gpio_num = LED_GPIO,
        .max_leds = 1,
    };
    led_strip_rmt_config_t rmt_cfg = {
        .resolution_hz = 10 * 1000 * 1000,  // 10 MHz
    };
    led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &led_strip);
    led_strip_clear(led_strip);
}

void rgb_led_set_range(float range_m)
{
    uint8_t r, g, b;
    range_to_rgb(range_m, &r, &g, &b);
    led_strip_set_pixel(led_strip, 0, r, g, b);
    led_strip_refresh(led_strip);
}

void rgb_led_off(void)
{
    led_strip_clear(led_strip);
}
