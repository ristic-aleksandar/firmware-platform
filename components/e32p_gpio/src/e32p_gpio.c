/**
 * @file e32p_gpio.c
 * @brief e32p_gpio implementation — thin facade + WROOM-32 pin-safety guards.
 */
#include <stdbool.h>
#include "esp_log.h"
#include "driver/gpio.h"
#include "e32p_gpio.h"

static const char *TAG = "e32p_gpio";

static gpio_mode_t map_mode(e32p_gpio_mode_t m)
{
    switch (m) {
    case E32P_GPIO_MODE_OUTPUT:          return GPIO_MODE_OUTPUT;
    case E32P_GPIO_MODE_OUTPUT_OD:       return GPIO_MODE_OUTPUT_OD;
    case E32P_GPIO_MODE_INPUT_OUTPUT_OD: return GPIO_MODE_INPUT_OUTPUT_OD;
    case E32P_GPIO_MODE_INPUT:
    default:                             return GPIO_MODE_INPUT;
    }
}

static gpio_int_type_t map_intr(e32p_gpio_intr_t it)
{
    switch (it) {
    case E32P_GPIO_INTR_RISING:     return GPIO_INTR_POSEDGE;
    case E32P_GPIO_INTR_FALLING:    return GPIO_INTR_NEGEDGE;
    case E32P_GPIO_INTR_ANYEDGE:    return GPIO_INTR_ANYEDGE;
    case E32P_GPIO_INTR_LOW_LEVEL:  return GPIO_INTR_LOW_LEVEL;
    case E32P_GPIO_INTR_HIGH_LEVEL: return GPIO_INTR_HIGH_LEVEL;
    case E32P_GPIO_INTR_NONE:
    default:                        return GPIO_INTR_DISABLE;
    }
}

/* Validate a pin/config against generic SoC limits and, when guards are on,
 * the ESP32-specific reserved/strapping/input-only rules. */
static esp_err_t validate_config(const e32p_gpio_config_t *cfg)
{
    E32P_CHECK_ARG(cfg != NULL, TAG);
    E32P_CHECK_ARG(GPIO_IS_VALID_GPIO(cfg->pin), TAG);

    const bool wants_output = (cfg->mode != E32P_GPIO_MODE_INPUT);
    ESP_RETURN_ON_FALSE(!wants_output || GPIO_IS_VALID_OUTPUT_GPIO(cfg->pin),
                        ESP_ERR_INVALID_ARG, TAG,
                        "GPIO%d is input-only, cannot drive output", cfg->pin);

#if CONFIG_E32P_PIN_GUARDS && CONFIG_IDF_TARGET_ESP32
    if (cfg->pin >= 6 && cfg->pin <= 11) {
        ESP_LOGE(TAG, "GPIO%d is a SPI-flash pin (6-11); refusing", cfg->pin);
        return ESP_ERR_E32P_PIN_RESERVED;
    }
    if (cfg->pin == 0 || cfg->pin == 2 || cfg->pin == 12 || cfg->pin == 15) {
        ESP_LOGW(TAG, "GPIO%d is a strapping pin; boot-time state matters", cfg->pin);
    }
    if (cfg->pin >= 34 && cfg->pin <= 39 && cfg->pull != E32P_GPIO_PULL_NONE) {
        ESP_LOGE(TAG, "GPIO%d has no internal pull resistor", cfg->pin);
        return ESP_ERR_INVALID_ARG;
    }
#endif
    return ESP_OK;
}

esp_err_t e32p_gpio_config(const e32p_gpio_config_t *cfg)
{
    E32P_RETURN_ON_ERR(validate_config(cfg), TAG);

    gpio_config_t io = {
        .pin_bit_mask = 1ULL << cfg->pin,
        .mode         = map_mode(cfg->mode),
        .pull_up_en   = (cfg->pull == E32P_GPIO_PULL_UP)   ? GPIO_PULLUP_ENABLE   : GPIO_PULLUP_DISABLE,
        .pull_down_en = (cfg->pull == E32P_GPIO_PULL_DOWN) ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE,
        .intr_type    = map_intr(cfg->intr),
    };
    return gpio_config(&io);
}

esp_err_t e32p_gpio_reset(int pin)
{
    E32P_CHECK_ARG(GPIO_IS_VALID_GPIO(pin), TAG);
    return gpio_reset_pin(pin);
}

esp_err_t e32p_gpio_set_level(int pin, uint32_t level)
{
    E32P_CHECK_ARG(GPIO_IS_VALID_OUTPUT_GPIO(pin), TAG);
    return gpio_set_level(pin, level);
}

esp_err_t e32p_gpio_get_level(int pin, int *out_level)
{
    E32P_CHECK_ARG(GPIO_IS_VALID_GPIO(pin), TAG);
    E32P_CHECK_ARG(out_level != NULL, TAG);
    *out_level = gpio_get_level(pin);
    return ESP_OK;
}

esp_err_t e32p_gpio_install_isr_service(int intr_alloc_flags)
{
    return gpio_install_isr_service(intr_alloc_flags);
}

esp_err_t e32p_gpio_uninstall_isr_service(void)
{
    gpio_uninstall_isr_service();
    return ESP_OK;
}

esp_err_t e32p_gpio_add_isr_handler(int pin, e32p_gpio_isr_t handler, void *arg)
{
    E32P_CHECK_ARG(GPIO_IS_VALID_GPIO(pin), TAG);
    E32P_CHECK_ARG(handler != NULL, TAG);
    return gpio_isr_handler_add(pin, handler, arg);
}

esp_err_t e32p_gpio_remove_isr_handler(int pin)
{
    E32P_CHECK_ARG(GPIO_IS_VALID_GPIO(pin), TAG);
    return gpio_isr_handler_remove(pin);
}

esp_err_t e32p_gpio_intr_enable(int pin)
{
    E32P_CHECK_ARG(GPIO_IS_VALID_GPIO(pin), TAG);
    return gpio_intr_enable(pin);
}

esp_err_t e32p_gpio_intr_disable(int pin)
{
    E32P_CHECK_ARG(GPIO_IS_VALID_GPIO(pin), TAG);
    return gpio_intr_disable(pin);
}
