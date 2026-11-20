#pragma once
/**
 * @file e32p_ledc.h
 * @brief Thin facade over driver/ledc.h: one handle == one PWM channel.
 *        Opaque-handle lifecycle (init/deinit/start/stop) with duty + fade.
 */
#include <stdint.h>
#include <stdbool.h>
#include "e32p_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Opaque PWM-channel handle. */
typedef struct e32p_ledc_ctx *e32p_ledc_handle_t;

/** @brief Channel configuration (caller-owned, passed by const pointer). */
typedef struct {
    int      pin;                   /*!< Output-capable GPIO. */
    uint32_t freq_hz;               /*!< PWM frequency, e.g. 5000. */
    uint8_t  duty_resolution_bits;  /*!< 1..SOC max; max duty = 2^bits - 1. */
    uint8_t  channel;               /*!< LEDC channel index (0..SOC max-1). */
    uint8_t  timer;                 /*!< LEDC timer index (0..3). */
    bool     high_speed;            /*!< ESP32 only; ignored where unsupported. */
    uint32_t initial_duty;          /*!< Duty applied on the first start(). */
} e32p_ledc_config_t;

/** @brief Defaults: 5 kHz, 13-bit, channel 0 / timer 0, idle at duty 0. */
#define E32P_LEDC_CONFIG_DEFAULT()  ((e32p_ledc_config_t){ \
    .pin = -1, .freq_hz = 5000, .duty_resolution_bits = 13, \
    .channel = 0, .timer = 0, .high_speed = true, .initial_duty = 0 })

/** @brief Runtime status snapshot. */
typedef struct {
    bool     running;    /*!< True between start() and stop(). */
    uint32_t duty;       /*!< Last requested duty (raw). */
    uint32_t max_duty;   /*!< 2^duty_resolution_bits - 1. */
    uint32_t freq_hz;    /*!< Configured frequency. */
} e32p_ledc_status_t;

/**
 * @brief Configure a PWM channel; leaves the output idle (call start()).
 * @param[in]  cfg  Channel configuration (must not be NULL).
 * @param[out] out  Receives the handle on success.
 * @return ESP_OK, ESP_ERR_INVALID_ARG, ESP_ERR_NO_MEM, or a driver error.
 */
esp_err_t e32p_ledc_init(const e32p_ledc_config_t *cfg, e32p_ledc_handle_t *out);

/**
 * @brief Stop output (if running) and release the handle.
 * @param[in] h  Handle (NULL is a no-op returning ESP_OK).
 * @return ESP_OK or a driver error.
 */
esp_err_t e32p_ledc_deinit(e32p_ledc_handle_t h);

/**
 * @brief Begin output at the current duty.
 * @param[in] h  Valid handle.
 * @return ESP_OK, ESP_ERR_INVALID_ARG, or a driver error.
 */
esp_err_t e32p_ledc_start(e32p_ledc_handle_t h);

/**
 * @brief Stop output; the pin idles low.
 * @param[in] h  Valid handle.
 * @return ESP_OK, ESP_ERR_INVALID_ARG, or a driver error.
 */
esp_err_t e32p_ledc_stop(e32p_ledc_handle_t h);

/**
 * @brief Set duty (raw). Applied live if running, else stored for next start().
 * @param[in] h     Valid handle.
 * @param[in] duty  0..max_duty (clamped to max_duty).
 * @return ESP_OK, ESP_ERR_INVALID_ARG, or a driver error.
 */
esp_err_t e32p_ledc_set_duty(e32p_ledc_handle_t h, uint32_t duty);

/**
 * @brief Set duty as a percentage.
 * @param[in] h        Valid handle.
 * @param[in] percent  0..100 (clamped).
 * @return ESP_OK, ESP_ERR_INVALID_ARG, or a driver error.
 */
esp_err_t e32p_ledc_set_duty_percent(e32p_ledc_handle_t h, uint8_t percent);

/**
 * @brief Hardware-fade to a target duty over a duration (starts output).
 * @param[in] h            Valid handle.
 * @param[in] target_duty  0..max_duty (clamped).
 * @param[in] ms           Fade duration in milliseconds.
 * @return ESP_OK, ESP_ERR_INVALID_ARG, or a driver error.
 */
esp_err_t e32p_ledc_fade(e32p_ledc_handle_t h, uint32_t target_duty, uint32_t ms);

/**
 * @brief Read current status.
 * @param[in]  h    Valid handle.
 * @param[out] out  Receives the snapshot.
 * @return ESP_OK or ESP_ERR_INVALID_ARG.
 */
esp_err_t e32p_ledc_get_status(e32p_ledc_handle_t h, e32p_ledc_status_t *out);

#ifdef __cplusplus
}
#endif
