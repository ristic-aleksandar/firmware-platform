/**
 * @file main.c
 * @brief 08_gptimer_alarm — a 1 Hz hardware-timer alarm counted over serial.
 *
 * Proves e32p_gptimer: a periodic alarm ISR increments a counter; main logs the
 * count and the raw microsecond tick. Fully observable with no external parts.
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "e32p_gptimer.h"

static const char *TAG = "gptimer";
static volatile uint32_t s_alarms = 0;

/* ISR context: keep it trivial. IRAM_ATTR so it is safe even if flash cache is off. */
static void IRAM_ATTR on_tick(void *arg)
{
    (void)arg;
    s_alarms++;
}

void app_main(void)
{
    e32p_gptimer_config_t cfg = {
        .resolution_hz      = 1000000,   /* 1 MHz -> 1 tick = 1 us */
        .alarm_period_ticks = 1000000,   /* alarm every 1,000,000 us = 1 s */
        .auto_reload        = true,
    };
    e32p_gptimer_handle_t tmr = NULL;
    ESP_ERROR_CHECK(e32p_gptimer_init(&cfg, on_tick, NULL, &tmr));
    ESP_ERROR_CHECK(e32p_gptimer_start(tmr));
    ESP_LOGI(TAG, "1 Hz hardware alarm running");

    while (1) {
        uint64_t ticks = 0;
        e32p_gptimer_get_count(tmr, &ticks);
        ESP_LOGI(TAG, "alarms=%lu  raw=%llu us", (unsigned long)s_alarms,
                 (unsigned long long)ticks);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
