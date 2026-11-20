#pragma once
/**
 * @file e32p_gptimer.h
 * @brief Thin facade over driver/gptimer.h: a hardware timer with a periodic or
 *        one-shot alarm callback. One handle per timer.
 */
#include <stdint.h>
#include <stdbool.h>
#include "e32p_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Opaque timer handle. */
typedef struct e32p_gptimer_ctx *e32p_gptimer_handle_t;

/** @brief Alarm callback. Runs in ISR context — keep it short, no blocking.
 *         Mark your callback IRAM_ATTR if it must run while flash cache is off. */
typedef void (*e32p_gptimer_cb_t)(void *arg);

/** @brief Timer configuration. */
typedef struct {
    uint32_t resolution_hz;       /*!< Ticks per second (e.g. 1000000 = 1 us/tick). */
    uint64_t alarm_period_ticks;  /*!< Fire the alarm every N ticks. */
    bool     auto_reload;         /*!< true = periodic, false = one-shot. */
} e32p_gptimer_config_t;

/**
 * @brief Create a timer and register its alarm. Left stopped — call start().
 * @param[in]  cfg  Configuration (must not be NULL).
 * @param[in]  cb   Alarm callback (ISR context; may be NULL).
 * @param[in]  arg  Opaque argument passed to @p cb.
 * @param[out] out  Receives the handle.
 * @return ESP_OK, ESP_ERR_INVALID_ARG, ESP_ERR_NO_MEM, or a driver error.
 */
esp_err_t e32p_gptimer_init(const e32p_gptimer_config_t *cfg, e32p_gptimer_cb_t cb,
                            void *arg, e32p_gptimer_handle_t *out);

/** @brief Stop (if running) and release the timer (NULL is a no-op). */
esp_err_t e32p_gptimer_deinit(e32p_gptimer_handle_t h);

/** @brief Begin counting and alarming. @return ESP_OK / ESP_ERR_INVALID_ARG / driver error. */
esp_err_t e32p_gptimer_start(e32p_gptimer_handle_t h);
/** @brief Pause counting. @return ESP_OK / ESP_ERR_INVALID_ARG / driver error. */
esp_err_t e32p_gptimer_stop(e32p_gptimer_handle_t h);

/**
 * @brief Read the current raw tick count.
 * @param[in]  h          Valid handle.
 * @param[out] out_ticks  Receives the count.
 * @return ESP_OK or ESP_ERR_INVALID_ARG.
 */
esp_err_t e32p_gptimer_get_count(e32p_gptimer_handle_t h, uint64_t *out_ticks);

#ifdef __cplusplus
}
#endif
