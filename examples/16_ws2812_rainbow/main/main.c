/**
 * @file main.c
 * @brief 16_ws2812_rainbow — proves e32p_ws2812 by scrolling a rainbow.
 *
 * Each frame assigns every pixel a hue (spread across the strip and advanced
 * over time), converts HSV->RGB, and refreshes. On a bare board this exercises
 * the full RMT TX + encoder path (a frame is clocked out every ~50 ms); wire a
 * WS2812 strip to GPIO18 (+5V, common GND) to watch the rainbow move.
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "e32p_ws2812.h"

static const char *TAG = "ws2812_rainbow";

#define STRIP_GPIO 18
#define NUM_LEDS   8

/* Minimal HSV->RGB. h in [0,360), s/v in [0,255]. */
static void hsv2rgb(uint16_t h, uint8_t s, uint8_t v,
                    uint8_t *r, uint8_t *g, uint8_t *b)
{
    h %= 360;
    const uint8_t region = h / 60;
    const uint8_t rem = (h - region * 60) * 255 / 60;
    const uint8_t p = (uint16_t)v * (255 - s) / 255;
    const uint8_t q = (uint16_t)v * (255 - (uint16_t)s * rem / 255) / 255;
    const uint8_t t = (uint16_t)v * (255 - (uint16_t)s * (255 - rem) / 255) / 255;
    switch (region) {
    case 0:  *r = v; *g = t; *b = p; break;
    case 1:  *r = q; *g = v; *b = p; break;
    case 2:  *r = p; *g = v; *b = t; break;
    case 3:  *r = p; *g = q; *b = v; break;
    case 4:  *r = t; *g = p; *b = v; break;
    default: *r = v; *g = p; *b = q; break;
    }
}

void app_main(void)
{
    e32p_ws2812_handle_t strip = NULL;
    ESP_ERROR_CHECK(e32p_ws2812_init(STRIP_GPIO, NUM_LEDS, &strip));
    ESP_LOGI(TAG, "scrolling rainbow on GPIO%d, %d LEDs", STRIP_GPIO, NUM_LEDS);

    uint16_t start = 0;
    uint32_t frame = 0;
    for (;;) {
        for (int i = 0; i < NUM_LEDS; i++) {
            uint8_t r, g, b;
            const uint16_t hue = (start + i * 360 / NUM_LEDS) % 360;
            hsv2rgb(hue, 255, 40 /* modest brightness */, &r, &g, &b);
            e32p_ws2812_set_pixel(strip, i, r, g, b);
        }
        ESP_ERROR_CHECK(e32p_ws2812_refresh(strip));

        if ((frame++ % 20) == 0) {
            ESP_LOGI(TAG, "frame %lu sent (hue base %u)",
                     (unsigned long)frame, (unsigned)start);
        }
        start = (start + 6) % 360;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
