#pragma once
/**
 * @file e32p_tm1637.h
 * @brief TM1637 4-digit 7-segment display over a bit-banged 2-wire bus.
 *
 * The TM1637 protocol looks like I2C (start/stop + per-byte ACK) but has no
 * device address and clocks data LSB-first, so it is bit-banged over two
 * open-drain GPIOs rather than driven by the I2C peripheral.
 *
 * Split into a PURE part (segment font + integer formatting — no GPIO, fully
 * unit-testable) and a THIN GPIO part (start/stop/write-byte + the display
 * commands). Opaque-handle style; one handle per display.
 */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "e32p_core.h"

#ifdef __cplusplus
extern "C" {
#endif

#define E32P_TM1637_DIGITS       4     /*!< Digits on the common 4-digit module. */
#define E32P_TM1637_MAX_BRIGHT   7     /*!< Brightness range is 0..7. */
#define E32P_TM1637_SEG_MINUS    0x40  /*!< Segment 'g' only — a minus sign. */
#define E32P_TM1637_SEG_COLON    0x80  /*!< Colon bit, ORed onto digit position 1. */

/** @brief Display configuration. */
typedef struct {
    int     clk_pin;     /*!< CLK GPIO (output-capable). */
    int     dio_pin;     /*!< DIO GPIO (output-capable, read back for ACK). */
    uint8_t brightness;  /*!< Initial brightness 0..7. */
} e32p_tm1637_config_t;

/** @brief Opaque per-display handle. */
typedef struct e32p_tm1637_ctx *e32p_tm1637_handle_t;

/* ---- Pure functions (no GPIO; unit-testable) ------------------------------ */

/**
 * @brief Map a hex nibble to its 7-segment byte.
 * @param[in] digit  0..15 (0-9, A-F).
 * @return Segment bitmap (bit0=a … bit6=g); 0 (blank) if @p digit > 15.
 */
uint8_t e32p_tm1637_encode_digit(uint8_t digit);

/**
 * @brief Format a signed integer into 4 right-aligned segment bytes.
 * @param[in]  value          -999..9999 (a leading '-' consumes one digit).
 * @param[in]  leading_zeros  Pad the high positions with '0' instead of blank.
 * @param[in]  colon          Light the colon (sets bit7 of position 1).
 * @param[out] out            Receives 4 segment bytes (position 0 = leftmost).
 * @return ESP_OK, or ESP_ERR_INVALID_ARG if @p out is NULL or @p value does not fit.
 */
esp_err_t e32p_tm1637_format_int(int value, bool leading_zeros, bool colon, uint8_t out[E32P_TM1637_DIGITS]);

/* ---- Device functions (bit-banged GPIO) ----------------------------------- */

/**
 * @brief Configure the two GPIOs and turn the display on at the given brightness.
 *
 * Probes for the part: the display-control byte must be acknowledged (DIO pulled
 * low). An unwired or unpowered display does not ACK, so init fails rather than
 * reporting a phantom success.
 *
 * @param[in]  cfg  Configuration (must not be NULL; brightness clamped to 0..7).
 * @param[out] out  Receives the handle.
 * @return ESP_OK, ESP_ERR_INVALID_ARG, ESP_ERR_NO_MEM, ESP_ERR_NOT_FOUND (no
 *         display acknowledged), or a driver error.
 */
esp_err_t e32p_tm1637_init(const e32p_tm1637_config_t *cfg, e32p_tm1637_handle_t *out);

/** @brief Reset the pins and free (NULL is a no-op). */
esp_err_t e32p_tm1637_deinit(e32p_tm1637_handle_t h);

/**
 * @brief Set brightness (0..7) and refresh the display-control command.
 * @return ESP_OK, ESP_ERR_INVALID_ARG, ESP_ERR_NOT_FOUND (no ACK), or a driver error.
 */
esp_err_t e32p_tm1637_set_brightness(e32p_tm1637_handle_t h, uint8_t level);

/**
 * @brief Write raw segment bytes starting at a digit position.
 * @param[in] h    Valid handle.
 * @param[in] seg  Segment bytes to write.
 * @param[in] len  Number of bytes (pos + len must be <= 4).
 * @param[in] pos  Starting digit (0 = leftmost).
 * @return ESP_OK, ESP_ERR_INVALID_ARG, ESP_ERR_NOT_FOUND (no ACK), or a driver error.
 */
esp_err_t e32p_tm1637_set_segments(e32p_tm1637_handle_t h, const uint8_t *seg, size_t len, uint8_t pos);

/**
 * @brief Format and show a signed integer (see e32p_tm1637_format_int()).
 * @return ESP_OK, ESP_ERR_INVALID_ARG (value out of range), ESP_ERR_NOT_FOUND
 *         (no ACK), or a driver error.
 */
esp_err_t e32p_tm1637_show_int(e32p_tm1637_handle_t h, int value, bool leading_zeros, bool colon);

/** @brief Blank all four digits. */
esp_err_t e32p_tm1637_clear(e32p_tm1637_handle_t h);

#ifdef __cplusplus
}
#endif
