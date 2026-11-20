#pragma once
/**
 * @file e32p_uart.h
 * @brief Thin facade over driver/uart.h: one handle == one UART port.
 */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "e32p_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Opaque UART-port handle. */
typedef struct e32p_uart_ctx *e32p_uart_handle_t;

/** @brief Parity mode. */
typedef enum {
    E32P_UART_PARITY_NONE,
    E32P_UART_PARITY_EVEN,
    E32P_UART_PARITY_ODD,
} e32p_uart_parity_t;

/** @brief Stop bits. */
typedef enum {
    E32P_UART_STOP_1,
    E32P_UART_STOP_2,
} e32p_uart_stopbits_t;

/** @brief Port configuration (caller-owned, passed by const pointer). */
typedef struct {
    int                  port;            /*!< 1 or 2 (0 is the console). */
    int                  tx_pin;          /*!< Output-capable GPIO. */
    int                  rx_pin;          /*!< Input-capable GPIO. */
    uint32_t             baud_rate;       /*!< e.g. 115200. */
    uint8_t              data_bits;       /*!< 5..8. */
    e32p_uart_parity_t   parity;
    e32p_uart_stopbits_t stop_bits;
    size_t               rx_buffer_size;  /*!< Driver RX ring buffer (>= 128). */
} e32p_uart_config_t;

/** @brief Defaults: 115200 8N1, 256-byte RX buffer. */
#define E32P_UART_CONFIG_DEFAULT()  ((e32p_uart_config_t){ \
    .port = -1, .tx_pin = -1, .rx_pin = -1, .baud_rate = 115200, \
    .data_bits = 8, .parity = E32P_UART_PARITY_NONE,             \
    .stop_bits = E32P_UART_STOP_1, .rx_buffer_size = 256 })

/**
 * @brief Configure and install a UART port.
 * @param[in]  cfg  Configuration (must not be NULL).
 * @param[out] out  Receives the handle on success.
 * @return ESP_OK, ESP_ERR_INVALID_ARG, ESP_ERR_NO_MEM, or a driver error.
 */
esp_err_t e32p_uart_init(const e32p_uart_config_t *cfg, e32p_uart_handle_t *out);

/**
 * @brief Uninstall the port and release the handle (NULL is a no-op).
 * @return ESP_OK or a driver error.
 */
esp_err_t e32p_uart_deinit(e32p_uart_handle_t h);

/**
 * @brief Write bytes (blocks until queued into the TX FIFO).
 * @return Number of bytes written, or -1 on error.
 */
int e32p_uart_write(e32p_uart_handle_t h, const void *data, size_t len);

/**
 * @brief Read up to @p len bytes, waiting at most @p timeout_ms.
 * @return Number of bytes read (may be < len on timeout), or -1 on error.
 */
int e32p_uart_read(e32p_uart_handle_t h, void *out, size_t len, uint32_t timeout_ms);

/** @brief Discard buffered RX data. @return ESP_OK or a driver error. */
esp_err_t e32p_uart_flush_input(e32p_uart_handle_t h);

/**
 * @brief Bytes currently buffered in the RX ring.
 * @param[in]  h          Valid handle.
 * @param[out] out_bytes  Receives the count.
 * @return ESP_OK or ESP_ERR_INVALID_ARG.
 */
esp_err_t e32p_uart_available(e32p_uart_handle_t h, size_t *out_bytes);

/**
 * @brief Enable/disable internal TX->RX loopback (self-test, no wiring).
 * @return ESP_OK or a driver error.
 */
esp_err_t e32p_uart_set_loopback(e32p_uart_handle_t h, bool enable);

#ifdef __cplusplus
}
#endif
