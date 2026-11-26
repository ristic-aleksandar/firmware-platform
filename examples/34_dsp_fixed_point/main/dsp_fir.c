#include "dsp_fir.h"

#include <stdlib.h>

#define DSP_Q 15

esp_err_t dsp_fir_q15(const int16_t *x, size_t n, const int16_t *h, size_t taps, int16_t *y)
{
    if (x == NULL || h == NULL || y == NULL || n == 0 || taps == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    /* The input can be -32768, so the worst case is 32768 * sum|h|. Keep it under 2^31. */
    int32_t sum_abs = 0;
    for (size_t k = 0; k < taps; k++) {
        sum_abs += abs((int)h[k]);
    }
    if (sum_abs >= 65536) {
        return ESP_ERR_INVALID_ARG;
    }

    for (size_t i = 0; i < n; i++) {
        int32_t acc = 1 << (DSP_Q - 1);                 /* rounds to nearest, not down */
        size_t kmax = (i + 1 < taps) ? (i + 1) : taps;  /* zero state before the block */
        for (size_t k = 0; k < kmax; k++) {
            acc += (int32_t)x[i - k] * h[k];
        }
        acc >>= DSP_Q;
        if (acc > INT16_MAX) {
            acc = INT16_MAX;
        } else if (acc < INT16_MIN) {
            acc = INT16_MIN;
        }
        y[i] = (int16_t)acc;
    }
    return ESP_OK;
}

void dsp_fir_f32(const float *x, size_t n, const float *h, size_t taps, float *y)
{
    for (size_t i = 0; i < n; i++) {
        float acc = 0.0f;
        size_t kmax = (i + 1 < taps) ? (i + 1) : taps;
        for (size_t k = 0; k < kmax; k++) {
            acc += x[i - k] * h[k];
        }
        y[i] = acc;
    }
}
