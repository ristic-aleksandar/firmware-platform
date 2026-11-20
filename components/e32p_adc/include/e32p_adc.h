#pragma once
/**
 * @file e32p_adc.h
 * @brief Thin facade over esp_adc oneshot + calibration. One handle per ADC unit.
 */
#include <stdint.h>
#include "e32p_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Opaque ADC-unit handle. */
typedef struct e32p_adc_ctx *e32p_adc_handle_t;

/** @brief Input attenuation → full-scale range (ESP32: ~1.1 / 1.5 / 2.2 / 3.9 V). */
typedef enum {
    E32P_ADC_ATTEN_0DB,
    E32P_ADC_ATTEN_2_5DB,
    E32P_ADC_ATTEN_6DB,
    E32P_ADC_ATTEN_12DB,   /*!< widest range, ~0..3.1 V usable. */
} e32p_adc_atten_t;

/**
 * @brief Initialize an ADC unit.
 * @param[in]  unit  1 (ADC1) or 2 (ADC2). ADC2 is unusable while Wi-Fi is on.
 * @param[out] out   Receives the handle.
 * @return ESP_OK, ESP_ERR_INVALID_ARG, ESP_ERR_NO_MEM, or a driver error.
 */
esp_err_t e32p_adc_init(int unit, e32p_adc_handle_t *out);

/** @brief Release the unit and any calibration schemes (NULL is a no-op). */
esp_err_t e32p_adc_deinit(e32p_adc_handle_t h);

/**
 * @brief Configure one channel (attenuation + default 12-bit width) and set up
 *        calibration for read_voltage when the chip supports it.
 * @param[in] h        Valid handle.
 * @param[in] channel  ADC channel index (see the GPIO map in the README).
 * @param[in] atten    Input attenuation.
 * @return ESP_OK, ESP_ERR_INVALID_ARG, or a driver error.
 */
esp_err_t e32p_adc_config_channel(e32p_adc_handle_t h, int channel, e32p_adc_atten_t atten);

/**
 * @brief Read a raw count (0..4095 at 12-bit).
 * @return ESP_OK, ESP_ERR_INVALID_ARG, ESP_ERR_E32P_RESOURCE_CONFLICT (ADC2 while
 *         Wi-Fi is active), or a driver error.
 */
esp_err_t e32p_adc_read_raw(e32p_adc_handle_t h, int channel, int *out_raw);

/**
 * @brief Read a calibrated voltage in millivolts (config_channel must run first).
 * @return ESP_OK, ESP_ERR_INVALID_STATE (calibration unavailable), or an error
 *         from e32p_adc_read_raw.
 */
esp_err_t e32p_adc_read_voltage(e32p_adc_handle_t h, int channel, int *out_mv);

#ifdef __cplusplus
}
#endif
