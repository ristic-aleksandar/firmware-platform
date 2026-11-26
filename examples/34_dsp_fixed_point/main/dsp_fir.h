/**
 * @file dsp_fir.h
 * @brief FIR filter in Q15 fixed point, and the same filter in float for comparison.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

/**
 * @brief Run a causal FIR filter on Q15 samples. The filter starts with a zero state.
 *
 * Each output is the sum of coef * sample in a 32-bit accumulator, plus half an LSB,
 * shifted right by 15 and saturated to int16. This matches the numpy model in
 * tools/gen_vectors.py bit for bit.
 *
 * The accumulator is 32-bit to stay fast on the ESP32. That is safe only when
 * the sum of the absolute coefficients is below 65536 (a Q15 gain under 2.0). The
 * function checks this and refuses to run otherwise.
 *
 * @param[in]  x     Input samples, Q15.
 * @param[in]  n     Number of samples.
 * @param[in]  h     Coefficients, Q15, h[0] applies to the newest sample.
 * @param[in]  taps  Number of coefficients.
 * @param[out] y     Output samples, Q15. Same length as the input. Must not overlap x.
 * @return ESP_OK, or ESP_ERR_INVALID_ARG if a pointer is NULL, or if taps or n is 0,
 *         or if the coefficients could overflow the accumulator.
 */
esp_err_t dsp_fir_q15(const int16_t *x, size_t n, const int16_t *h, size_t taps, int16_t *y);

/**
 * @brief The same filter in single-precision float, used only to compare speed.
 *
 * @param[in]  x     Input samples, as float in the range -1.0 to 1.0.
 * @param[in]  n     Number of samples.
 * @param[in]  h     Coefficients as float.
 * @param[in]  taps  Number of coefficients.
 * @param[out] y     Output samples. Same length as the input. Must not overlap x.
 */
void dsp_fir_f32(const float *x, size_t n, const float *h, size_t taps, float *y);
