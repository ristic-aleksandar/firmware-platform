#pragma once
/**
 * @file e32p_gpio.h
 * @brief Thin facade over driver/gpio.h: per-pin digital I/O with WROOM-32
 *        pin-safety guards. Flat/stateless style (pins are global resources).
 */
#include <stdint.h>
#include "e32p_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Pin direction / drive mode. */
typedef enum {
    E32P_GPIO_MODE_INPUT,           /*!< Input only. */
    E32P_GPIO_MODE_OUTPUT,          /*!< Push-pull output. */
    E32P_GPIO_MODE_OUTPUT_OD,       /*!< Open-drain output. */
    E32P_GPIO_MODE_INPUT_OUTPUT_OD, /*!< Open-drain, readable (e.g. 1-Wire/I2C-like). */
} e32p_gpio_mode_t;

/** @brief Internal pull resistor selection. */
typedef enum {
    E32P_GPIO_PULL_NONE,
    E32P_GPIO_PULL_UP,
    E32P_GPIO_PULL_DOWN,
} e32p_gpio_pull_t;

/** @brief Edge/level interrupt trigger. */
typedef enum {
    E32P_GPIO_INTR_NONE,
    E32P_GPIO_INTR_RISING,
    E32P_GPIO_INTR_FALLING,
    E32P_GPIO_INTR_ANYEDGE,
    E32P_GPIO_INTR_LOW_LEVEL,
    E32P_GPIO_INTR_HIGH_LEVEL,
} e32p_gpio_intr_t;

/** @brief Single-pin configuration (caller-owned, passed by const pointer). */
typedef struct {
    int              pin;   /*!< GPIO number. */
    e32p_gpio_mode_t mode;  /*!< Direction / drive. */
    e32p_gpio_pull_t pull;  /*!< Internal pull (ignored on GPIO34-39). */
    e32p_gpio_intr_t intr;  /*!< Interrupt trigger; NONE for plain I/O. */
} e32p_gpio_config_t;

/** @brief Defaults: an unconfigured input, no pull, no interrupt. */
#define E32P_GPIO_CONFIG_DEFAULT()  ((e32p_gpio_config_t){ \
    .pin  = -1,                                            \
    .mode = E32P_GPIO_MODE_INPUT,                          \
    .pull = E32P_GPIO_PULL_NONE,                           \
    .intr = E32P_GPIO_INTR_NONE,                           \
})

/** @brief ISR callback. Runs in interrupt context: keep short, no blocking. */
typedef void (*e32p_gpio_isr_t)(void *arg);

/**
 * @brief Configure one pin.
 * @param[in] cfg  Pin configuration (must not be NULL).
 * @return ESP_OK; ESP_ERR_INVALID_ARG (bad pin / input-only used as output /
 *         pull on a pin without one); ESP_ERR_E32P_PIN_RESERVED (SPI-flash pin,
 *         guards on); or a driver error.
 */
esp_err_t e32p_gpio_config(const e32p_gpio_config_t *cfg);

/**
 * @brief Reset a pin to its default (disconnected input) state.
 * @param[in] pin  GPIO number.
 * @return ESP_OK, ESP_ERR_INVALID_ARG, or a driver error.
 */
esp_err_t e32p_gpio_reset(int pin);

/**
 * @brief Drive an output pin.
 * @param[in] pin    Output-capable GPIO number.
 * @param[in] level  0 = low, non-zero = high.
 * @return ESP_OK, ESP_ERR_INVALID_ARG (not output-capable), or a driver error.
 */
esp_err_t e32p_gpio_set_level(int pin, uint32_t level);

/**
 * @brief Read a pin's level.
 * @param[in]  pin        GPIO number.
 * @param[out] out_level  Receives 0 or 1.
 * @return ESP_OK or ESP_ERR_INVALID_ARG.
 */
esp_err_t e32p_gpio_get_level(int pin, int *out_level);

/**
 * @brief Install the shared GPIO ISR dispatch service (call once).
 * @param[in] intr_alloc_flags  ESP_INTR_FLAG_* flags (0 for defaults).
 * @return ESP_OK, ESP_ERR_INVALID_STATE (already installed), or a driver error.
 */
esp_err_t e32p_gpio_install_isr_service(int intr_alloc_flags);

/**
 * @brief Uninstall the shared GPIO ISR service.
 * @return ESP_OK.
 */
esp_err_t e32p_gpio_uninstall_isr_service(void);

/**
 * @brief Attach a per-pin ISR handler (requires the ISR service installed).
 * @param[in] pin      GPIO number.
 * @param[in] handler  Callback run in ISR context (must not be NULL).
 * @param[in] arg      Opaque argument passed to @p handler.
 * @return ESP_OK, ESP_ERR_INVALID_ARG, or a driver error.
 */
esp_err_t e32p_gpio_add_isr_handler(int pin, e32p_gpio_isr_t handler, void *arg);

/**
 * @brief Detach a per-pin ISR handler.
 * @param[in] pin  GPIO number.
 * @return ESP_OK, ESP_ERR_INVALID_ARG, or a driver error.
 */
esp_err_t e32p_gpio_remove_isr_handler(int pin);

/**
 * @brief Enable interrupts on a configured pin.
 * @param[in] pin  GPIO number.
 * @return ESP_OK, ESP_ERR_INVALID_ARG, or a driver error.
 */
esp_err_t e32p_gpio_intr_enable(int pin);

/**
 * @brief Disable interrupts on a pin.
 * @param[in] pin  GPIO number.
 * @return ESP_OK, ESP_ERR_INVALID_ARG, or a driver error.
 */
esp_err_t e32p_gpio_intr_disable(int pin);

#ifdef __cplusplus
}
#endif
