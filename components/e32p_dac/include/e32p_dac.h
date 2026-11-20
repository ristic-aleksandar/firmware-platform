#pragma once
/**
 * @file e32p_dac.h
 * @brief Thin facade over driver/dac_oneshot.h: 8-bit DAC output. ESP32 has two
 *        channels — ch0 = GPIO25, ch1 = GPIO26.
 */
#include <stdint.h>
#include "e32p_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Opaque DAC-channel handle. */
typedef struct e32p_dac_ctx *e32p_dac_handle_t;

/**
 * @brief Initialize a DAC channel.
 * @param[in]  channel  0 (GPIO25) or 1 (GPIO26).
 * @param[out] out      Receives the handle.
 * @return ESP_OK, ESP_ERR_INVALID_ARG, ESP_ERR_NO_MEM, or a driver error.
 */
esp_err_t e32p_dac_init(int channel, e32p_dac_handle_t *out);

/** @brief Release the channel (NULL is a no-op). */
esp_err_t e32p_dac_deinit(e32p_dac_handle_t h);

/** @brief Output a raw 8-bit level (0..255 maps ~0..Vdd). */
esp_err_t e32p_dac_set_raw(e32p_dac_handle_t h, uint8_t value);

/** @brief Output an approximate voltage in millivolts (0..~3300, clamped). */
esp_err_t e32p_dac_set_voltage(e32p_dac_handle_t h, uint32_t mv);

#ifdef __cplusplus
}
#endif
