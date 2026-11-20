#pragma once
/**
 * @file e32p_i2s.h
 * @brief Thin facade over driver/i2s_std.h: I2S standard (Philips) mode audio.
 *        One handle = one channel (TX for playback or RX for capture).
 */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "e32p_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Opaque I2S-channel handle. */
typedef struct e32p_i2s_ctx *e32p_i2s_handle_t;

/** @brief Channel direction. */
typedef enum { E32P_I2S_TX, E32P_I2S_RX } e32p_i2s_dir_t;

/** @brief Channel configuration (master, Philips standard format). */
typedef struct {
    int            port;          /*!< I2S port 0 or 1. */
    e32p_i2s_dir_t dir;           /*!< TX (playback) or RX (capture). */
    int            bclk_gpio;     /*!< Bit clock. */
    int            ws_gpio;       /*!< Word select (LRCLK). */
    int            data_gpio;     /*!< DOUT for TX, DIN for RX. */
    uint32_t       sample_rate;   /*!< e.g. 16000, 44100, 48000. */
    uint8_t        bits;          /*!< 16 or 32. */
    bool           stereo;        /*!< true = stereo, false = mono. */
} e32p_i2s_config_t;

/**
 * @brief Create, configure (std mode) and enable an I2S channel.
 * @return ESP_OK, ESP_ERR_INVALID_ARG, ESP_ERR_NO_MEM, or a driver error.
 */
esp_err_t e32p_i2s_init(const e32p_i2s_config_t *cfg, e32p_i2s_handle_t *out);
esp_err_t e32p_i2s_deinit(e32p_i2s_handle_t h);

/**
 * @brief Write PCM samples (TX channel).
 * @param[out] out_written  Bytes actually written (may be NULL).
 * @return ESP_OK, ESP_ERR_INVALID_ARG, ESP_ERR_INVALID_STATE (not a TX channel),
 *         or a driver error.
 */
esp_err_t e32p_i2s_write(e32p_i2s_handle_t h, const void *data, size_t len,
                         size_t *out_written, uint32_t timeout_ms);

/**
 * @brief Read PCM samples (RX channel).
 * @param[out] out_read  Bytes actually read (may be NULL).
 * @return ESP_OK, ESP_ERR_INVALID_ARG, ESP_ERR_INVALID_STATE (not an RX channel),
 *         or a driver error.
 */
esp_err_t e32p_i2s_read(e32p_i2s_handle_t h, void *buf, size_t len,
                        size_t *out_read, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
