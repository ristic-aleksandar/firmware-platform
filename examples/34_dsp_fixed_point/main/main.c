/*
 * 34_dsp_fixed_point: a FIR filter ported from Python (float) to C (Q15 fixed point).
 *
 * The test vectors come from tools/gen_vectors.py. The program runs the filter on the
 * chip and checks two things:
 *   1. The output equals the numpy model of the integer code, bit for bit.
 *   2. The output stays within DSP_MAX_ERR_LSB of the float reference.
 * It then measures how long the Q15 and the float versions take, and prints the result.
 */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include "esp_log.h"
#include "sdkconfig.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "dsp_fir.h"
#include "dsp_vectors.h"

#define TIMING_RUNS 200

static const char *TAG = "dsp34";

/* Both versions read their data from RAM, so the speed comparison is fair. A real filter
 * gets its input from an ADC or I2S buffer in RAM, not from flash. */
static int16_t s_in_q15[DSP_SAMPLES];
static int16_t s_coef_q15[DSP_TAPS];
static int16_t s_out_q15[DSP_SAMPLES];
static float s_in_f32[DSP_SAMPLES];
static float s_out_f32[DSP_SAMPLES];
static float s_coef_f32[DSP_TAPS];

/** @brief Average time of one block in microseconds, measured over TIMING_RUNS runs. */
static int64_t time_q15_us(void)
{
    int64_t t0 = esp_timer_get_time();
    for (int r = 0; r < TIMING_RUNS; r++) {
        (void)dsp_fir_q15(s_in_q15, DSP_SAMPLES, s_coef_q15, DSP_TAPS, s_out_q15);
    }
    return (esp_timer_get_time() - t0) / TIMING_RUNS;
}

static int64_t time_f32_us(void)
{
    int64_t t0 = esp_timer_get_time();
    for (int r = 0; r < TIMING_RUNS; r++) {
        dsp_fir_f32(s_in_f32, DSP_SAMPLES, s_coef_f32, DSP_TAPS, s_out_f32);
    }
    return (esp_timer_get_time() - t0) / TIMING_RUNS;
}

void app_main(void)
{
    for (int i = 0; i < DSP_SAMPLES; i++) {
        s_in_q15[i] = DSP_INPUT_Q15[i];
    }
    for (int k = 0; k < DSP_TAPS; k++) {
        s_coef_q15[k] = DSP_COEF_Q15[k];
    }

    /* One run for the correctness check. */
    esp_err_t err = dsp_fir_q15(s_in_q15, DSP_SAMPLES, s_coef_q15, DSP_TAPS, s_out_q15);
    ESP_ERROR_CHECK(err);

    int mismatches = 0;
    int max_err = 0;
    int64_t sum_err = 0;
    for (int i = 0; i < DSP_SAMPLES; i++) {
        if (s_out_q15[i] != DSP_MODEL_Q15[i]) {
            mismatches++;
        }
        int e = abs((int)s_out_q15[i] - (int)DSP_REF_Q15[i]);
        sum_err += e;
        if (e > max_err) {
            max_err = e;
        }
    }

    /* The float version, for the speed comparison. */
    for (int i = 0; i < DSP_SAMPLES; i++) {
        s_in_f32[i] = (float)DSP_INPUT_Q15[i] / 32768.0f;
    }
    for (int k = 0; k < DSP_TAPS; k++) {
        s_coef_f32[k] = (float)DSP_COEF_F32_E9[k] / 1e9f;
    }

    int64_t q15_us = time_q15_us();
    int64_t f32_us = time_f32_us();
    uint32_t cpu_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ;
    uint32_t macs = (uint32_t)DSP_SAMPLES * DSP_TAPS;   /* close to the real count */
    uint32_t q15_cycles_per_mac = (uint32_t)((q15_us * cpu_mhz * 10) / macs);
    uint32_t f32_cycles_per_mac = (uint32_t)((f32_us * cpu_mhz * 10) / macs);

    ESP_LOGI(TAG, "block: %d samples, %d taps, CPU %" PRIu32 " MHz", DSP_SAMPLES, DSP_TAPS, cpu_mhz);
    ESP_LOGI(TAG, "bit-exact vs numpy model: %d mismatches (must be 0)", mismatches);
    ESP_LOGI(TAG, "error vs float reference: max %d LSB, mean %" PRId64 ".%03" PRId64 " LSB (limit %d)",
             max_err, sum_err / DSP_SAMPLES, (sum_err * 1000 / DSP_SAMPLES) % 1000,
             DSP_MAX_ERR_LSB);
    ESP_LOGI(TAG, "time per block: Q15 %" PRId64 " us, float32 %" PRId64 " us", q15_us, f32_us);
    ESP_LOGI(TAG, "cycles per tap-sample: Q15 %" PRIu32 ".%" PRIu32 ", float32 %" PRIu32 ".%" PRIu32,
             q15_cycles_per_mac / 10, q15_cycles_per_mac % 10,
             f32_cycles_per_mac / 10, f32_cycles_per_mac % 10);

    bool pass = (mismatches == 0) && (max_err <= DSP_MAX_ERR_LSB);
    ESP_LOGI(TAG, "RESULT %s", pass ? "PASS" : "FAIL");

    /* Say it again every few seconds, so a monitor opened late still sees the result. */
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(3000));
        ESP_LOGI(TAG, "RESULT %s (max %d LSB, %d mismatches, Q15 %" PRId64 " us, float32 %" PRId64 " us)",
                 pass ? "PASS" : "FAIL", max_err, mismatches, q15_us, f32_us);
    }
}
