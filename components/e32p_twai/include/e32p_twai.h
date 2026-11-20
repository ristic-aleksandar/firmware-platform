#pragma once
/**
 * @file e32p_twai.h
 * @brief Thin facade over driver/twai.h: CAN 2.0 (TWAI) controller. The ESP32
 *        has one TWAI peripheral, so this API is a singleton (no handle).
 *
 * A real bus needs an external transceiver (e.g. SN65HVD230). For a wire-only
 * loopback test, use NO_ACK mode + a self frame and jumper TX to RX.
 */
#include <stdint.h>
#include <stdbool.h>
#include "e32p_core.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    E32P_TWAI_MODE_NORMAL,      /*!< Normal bus operation (needs a transceiver + peers). */
    E32P_TWAI_MODE_NO_ACK,      /*!< Self-test: TX/RX own frames, no ACK required. */
    E32P_TWAI_MODE_LISTEN_ONLY, /*!< Receive only, never drive the bus. */
} e32p_twai_mode_t;

typedef struct {
    int              tx_gpio;
    int              rx_gpio;
    uint32_t         bitrate_bps;  /*!< 125000 / 250000 / 500000 / 1000000. */
    e32p_twai_mode_t mode;
} e32p_twai_config_t;

typedef struct {
    uint32_t id;         /*!< 11-bit, or 29-bit when extended. */
    bool     extended;   /*!< Extended (29-bit) identifier. */
    bool     self;       /*!< Request self-reception (NO_ACK self-test). */
    uint8_t  len;        /*!< Data length 0..8. */
    uint8_t  data[8];
} e32p_twai_message_t;

/** @brief Install the TWAI driver. @return ESP_OK, ESP_ERR_INVALID_ARG,
 *         ESP_ERR_INVALID_STATE (already installed), or a driver error. */
esp_err_t e32p_twai_init(const e32p_twai_config_t *cfg);
esp_err_t e32p_twai_deinit(void);
esp_err_t e32p_twai_start(void);
esp_err_t e32p_twai_stop(void);

/** @brief Queue a frame for transmission. */
esp_err_t e32p_twai_transmit(const e32p_twai_message_t *msg, uint32_t timeout_ms);
/** @brief Receive a frame. @return ESP_OK, ESP_ERR_TIMEOUT, or a driver error. */
esp_err_t e32p_twai_receive(e32p_twai_message_t *msg, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
