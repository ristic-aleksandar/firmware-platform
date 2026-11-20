#pragma once
/**
 * @file e32p_touch.h
 * @brief Thin facade over the ESP32 touch sensor: init, configure a pad, read
 *        its raw capacitance value. "Is it touched?" is left to the app (compare
 *        against an untouched baseline) — see the example.
 */
#include <stdint.h>
#include "e32p_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Initialize the touch subsystem (idempotent). */
esp_err_t e32p_touch_init(void);

/** @brief Deinitialize the touch subsystem. */
esp_err_t e32p_touch_deinit(void);

/**
 * @brief Enable a touch pad (channel).
 * @param[in] pad  Touch channel 0..9 (T0..T9). ESP32 map: T0=GPIO4, T1=GPIO0,
 *                 T2=GPIO2, T3=GPIO15, T4=GPIO13, T5=GPIO12, T6=GPIO14,
 *                 T7=GPIO27, T8=GPIO33, T9=GPIO32.
 * @return ESP_OK, ESP_ERR_E32P_NOT_INIT, ESP_ERR_INVALID_ARG, or a driver error.
 */
esp_err_t e32p_touch_config_pad(int pad);

/**
 * @brief Read a pad's raw value. On ESP32 the value *drops* when touched (a
 *        capacitance discharge counter); compare against an untouched baseline.
 * @param[in]  pad        Configured touch channel.
 * @param[out] out_value  Receives the reading.
 * @return ESP_OK, ESP_ERR_E32P_NOT_INIT, ESP_ERR_INVALID_ARG, or a driver error.
 */
esp_err_t e32p_touch_read(int pad, uint32_t *out_value);

#ifdef __cplusplus
}
#endif
