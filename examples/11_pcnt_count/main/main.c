/**
 * @file main.c
 * @brief 11_pcnt_count — counts internally-generated pulses with PCNT.
 *
 * Proves e32p_pcnt + e32p_gpio with no wiring: io_loop_back feeds GPIO18's own
 * output back to the PCNT input, so toggling the pin generates countable edges.
 * Each cycle generates a fixed number of pulses and checks the count matches.
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "e32p_pcnt.h"
#include "e32p_gpio.h"

#define PCNT_GPIO   18
#define PULSES      50

static const char *TAG = "pcnt";

void app_main(void)
{
    e32p_pcnt_config_t cfg = {
        .edge_gpio = PCNT_GPIO, .high_limit = 1000, .low_limit = -1000,
        .count_rising = true, .count_falling = false, .io_loop_back = true,
    };
    e32p_pcnt_handle_t pcnt = NULL;
    ESP_ERROR_CHECK(e32p_pcnt_init(&cfg, &pcnt));
    ESP_ERROR_CHECK(e32p_pcnt_start(pcnt));
    ESP_LOGI(TAG, "counting rising edges on GPIO%d (internally generated, no wiring)", PCNT_GPIO);

    while (1) {
        ESP_ERROR_CHECK(e32p_pcnt_clear(pcnt));
        for (int i = 0; i < PULSES; i++) {
            e32p_gpio_set_level(PCNT_GPIO, 1);
            e32p_gpio_set_level(PCNT_GPIO, 0);
        }
        int count = 0;
        ESP_ERROR_CHECK(e32p_pcnt_get_count(pcnt, &count));
        ESP_LOGI(TAG, "generated %d pulses -> pcnt count = %d  [%s]",
                 PULSES, count, (count == PULSES) ? "PASS" : "FAIL");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
