/**
 * @file main.c
 * @brief 01_pwm_fade — breathes an LED on GPIO18 using the e32p_ledc facade.
 *
 * Proves the M1 slice: e32p_ledc's opaque-handle lifecycle (init -> start),
 * hardware fade, and status read. On a bare board the phase is visible over
 * serial; with an LED wired to GPIO18 it breathes up and down.
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "e32p_ledc.h"

#define PWM_PIN   18
#define FADE_MS   1000

static const char *TAG = "pwm_fade";

void app_main(void)
{
    e32p_ledc_config_t cfg = E32P_LEDC_CONFIG_DEFAULT();
    cfg.pin = PWM_PIN;

    e32p_ledc_handle_t pwm = NULL;
    ESP_ERROR_CHECK(e32p_ledc_init(&cfg, &pwm));
    ESP_ERROR_CHECK(e32p_ledc_start(pwm));

    e32p_ledc_status_t st;
    ESP_ERROR_CHECK(e32p_ledc_get_status(pwm, &st));
    ESP_LOGI(TAG, "fading on GPIO%d, max_duty=%lu", PWM_PIN, (unsigned long)st.max_duty);

    while (1) {
        ESP_ERROR_CHECK(e32p_ledc_fade(pwm, st.max_duty, FADE_MS));
        ESP_LOGI(TAG, "fade -> HIGH over %d ms", FADE_MS);
        vTaskDelay(pdMS_TO_TICKS(FADE_MS + 200));

        ESP_ERROR_CHECK(e32p_ledc_fade(pwm, 0, FADE_MS));
        ESP_LOGI(TAG, "fade -> LOW over %d ms", FADE_MS);
        vTaskDelay(pdMS_TO_TICKS(FADE_MS + 200));
    }
}
