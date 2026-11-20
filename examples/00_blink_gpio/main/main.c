/**
 * @file main.c
 * @brief 00_blink_gpio — blinks an LED on GPIO2 using the e32p_gpio facade.
 *
 * Proves the M0 slice end to end: e32p_core (version accessor) and e32p_gpio
 * (config + drive) compile, link, and run on a real WROOM-32.
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "e32p_gpio.h"

#define LED_PIN    2
#define BLINK_MS   500

static const char *TAG = "blink";

void app_main(void)
{
    ESP_LOGI(TAG, "esp32_platform %s - blink on GPIO%d", e32p_version_string(), LED_PIN);

    e32p_gpio_config_t led = E32P_GPIO_CONFIG_DEFAULT();
    led.pin  = LED_PIN;
    led.mode = E32P_GPIO_MODE_OUTPUT;
    ESP_ERROR_CHECK(e32p_gpio_config(&led));

    int level = 0;
    while (1) {
        /* Drive the pin (visible if an LED is wired to LED_PIN) and log the
         * toggle so the blink is observable over serial on a bare board too. */
        ESP_ERROR_CHECK(e32p_gpio_set_level(LED_PIN, level));
        ESP_LOGI(TAG, "GPIO%d -> %s", LED_PIN, level ? "HIGH" : "LOW");
        level = !level;
        vTaskDelay(pdMS_TO_TICKS(BLINK_MS));
    }
}
