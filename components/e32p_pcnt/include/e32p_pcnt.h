#pragma once
/**
 * @file e32p_pcnt.h
 * @brief Thin facade over driver/pulse_cnt.h (v5.x). One handle = one PCNT unit
 *        counting edges on a GPIO.
 */
#include <stdint.h>
#include <stdbool.h>
#include "e32p_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Opaque PCNT-unit handle. */
typedef struct e32p_pcnt_ctx *e32p_pcnt_handle_t;

/** @brief PCNT configuration. */
typedef struct {
    int  edge_gpio;       /*!< GPIO whose edges are counted. */
    int  high_limit;      /*!< Counter high watch-point (>0; ESP32 max 32767). */
    int  low_limit;       /*!< Counter low watch-point (<0; ESP32 min -32768). */
    bool count_rising;    /*!< Count rising edges. */
    bool count_falling;   /*!< Count falling edges. */
    bool io_loop_back;    /*!< Test mode: feed the GPIO's own output back to input,
                               so gpio_set_level() on edge_gpio generates counts. */
} e32p_pcnt_config_t;

/**
 * @brief Create a PCNT unit + edge channel. Left stopped — call start().
 * @param[in]  cfg  Configuration (must not be NULL).
 * @param[out] out  Receives the handle.
 * @return ESP_OK, ESP_ERR_INVALID_ARG, ESP_ERR_NO_MEM, or a driver error.
 */
esp_err_t e32p_pcnt_init(const e32p_pcnt_config_t *cfg, e32p_pcnt_handle_t *out);
esp_err_t e32p_pcnt_deinit(e32p_pcnt_handle_t h);

esp_err_t e32p_pcnt_start(e32p_pcnt_handle_t h);
esp_err_t e32p_pcnt_stop(e32p_pcnt_handle_t h);
/** @brief Reset the counter to 0. */
esp_err_t e32p_pcnt_clear(e32p_pcnt_handle_t h);
/** @brief Read the current signed count. */
esp_err_t e32p_pcnt_get_count(e32p_pcnt_handle_t h, int *out_count);

#ifdef __cplusplus
}
#endif
