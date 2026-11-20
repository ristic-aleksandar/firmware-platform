/**
 * @file main.c
 * @brief 10_touch_read — reads capacitive touch pad T0 (GPIO4) and flags a touch.
 *
 * Proves e32p_touch. Fully interactive with just a finger: touch or hover over
 * the GPIO4 pin on the header and the reading drops below the baseline.
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "e32p_touch.h"

#define TOUCH_CH   0   /* T0 = GPIO4 */

static const char *TAG = "touch";

void app_main(void)
{
    ESP_ERROR_CHECK(e32p_touch_init());
    ESP_ERROR_CHECK(e32p_touch_config_pad(TOUCH_CH));
    vTaskDelay(pdMS_TO_TICKS(100));   /* let the measurement FSM settle */

    /* Sample an untouched baseline. */
    uint32_t sum = 0;
    for (int i = 0; i < 16; i++) {
        uint32_t v = 0;
        e32p_touch_read(TOUCH_CH, &v);
        sum += v;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    const uint32_t baseline = sum / 16;
    const uint32_t threshold = baseline * 4 / 5;   /* touched when < 80% of baseline */
    ESP_LOGI(TAG, "T%d (GPIO4) baseline=%lu threshold=%lu - touch the GPIO4 pin to trigger",
             TOUCH_CH, (unsigned long)baseline, (unsigned long)threshold);

    while (1) {
        uint32_t v = 0;
        ESP_ERROR_CHECK(e32p_touch_read(TOUCH_CH, &v));
        ESP_LOGI(TAG, "touch=%4lu %s", (unsigned long)v, (v < threshold) ? "[TOUCHED]" : "");
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}
