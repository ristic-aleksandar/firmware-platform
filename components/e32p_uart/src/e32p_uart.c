/**
 * @file e32p_uart.c
 * @brief e32p_uart implementation — opaque-handle UART facade over driver/uart.h.
 */
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "e32p_uart.h"

static const char *TAG = "e32p_uart";

struct e32p_uart_ctx {
    int port;
};

static uart_parity_t map_parity(e32p_uart_parity_t p)
{
    switch (p) {
    case E32P_UART_PARITY_EVEN: return UART_PARITY_EVEN;
    case E32P_UART_PARITY_ODD:  return UART_PARITY_ODD;
    case E32P_UART_PARITY_NONE:
    default:                    return UART_PARITY_DISABLE;
    }
}

esp_err_t e32p_uart_init(const e32p_uart_config_t *cfg, e32p_uart_handle_t *out)
{
    E32P_CHECK_ARG(cfg != NULL, TAG);
    E32P_CHECK_ARG(out != NULL, TAG);
    E32P_CHECK_ARG(cfg->port > 0 && cfg->port < UART_NUM_MAX, TAG);   /* 0 = console */
    E32P_CHECK_ARG(GPIO_IS_VALID_OUTPUT_GPIO(cfg->tx_pin), TAG);
    E32P_CHECK_ARG(GPIO_IS_VALID_GPIO(cfg->rx_pin), TAG);
    E32P_CHECK_ARG(cfg->baud_rate > 0, TAG);
    E32P_CHECK_ARG(cfg->data_bits >= 5 && cfg->data_bits <= 8, TAG);
    E32P_CHECK_ARG(cfg->rx_buffer_size >= 128, TAG);

    const uart_config_t uc = {
        .baud_rate  = (int)cfg->baud_rate,
        .data_bits  = (uart_word_length_t)(cfg->data_bits - 5),   /* 5BIT==0 .. 8BIT==3 */
        .parity     = map_parity(cfg->parity),
        .stop_bits  = (cfg->stop_bits == E32P_UART_STOP_2) ? UART_STOP_BITS_2 : UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    /* Install first; on later failure we must delete the driver. */
    E32P_RETURN_ON_ERR(uart_driver_install(cfg->port, (int)cfg->rx_buffer_size, 0, 0, NULL, 0), TAG);

    esp_err_t ret = ESP_OK;
    struct e32p_uart_ctx *ctx = NULL;

    E32P_GOTO_ON_ERR(uart_param_config(cfg->port, &uc), TAG, fail);
    E32P_GOTO_ON_ERR(uart_set_pin(cfg->port, cfg->tx_pin, cfg->rx_pin,
                                  UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE), TAG, fail);

    ctx = calloc(1, sizeof(*ctx));
    ESP_GOTO_ON_FALSE(ctx != NULL, ESP_ERR_NO_MEM, fail, TAG, "alloc failed");
    ctx->port = cfg->port;
    *out = ctx;
    ESP_LOGI(TAG, "uart%d init tx=%d rx=%d baud=%lu", cfg->port, cfg->tx_pin,
             cfg->rx_pin, (unsigned long)cfg->baud_rate);
    return ESP_OK;

fail:
    uart_driver_delete(cfg->port);
    return ret;
}

esp_err_t e32p_uart_deinit(e32p_uart_handle_t h)
{
    if (h == NULL) {
        return ESP_OK;
    }
    uart_driver_delete(h->port);
    free(h);
    return ESP_OK;
}

int e32p_uart_write(e32p_uart_handle_t h, const void *data, size_t len)
{
    if (h == NULL || data == NULL) {
        return -1;
    }
    return uart_write_bytes(h->port, data, len);
}

int e32p_uart_read(e32p_uart_handle_t h, void *out, size_t len, uint32_t timeout_ms)
{
    if (h == NULL || out == NULL) {
        return -1;
    }
    return uart_read_bytes(h->port, out, len, pdMS_TO_TICKS(timeout_ms));
}

esp_err_t e32p_uart_flush_input(e32p_uart_handle_t h)
{
    E32P_CHECK_ARG(h != NULL, TAG);
    return uart_flush_input(h->port);
}

esp_err_t e32p_uart_available(e32p_uart_handle_t h, size_t *out_bytes)
{
    E32P_CHECK_ARG(h != NULL, TAG);
    E32P_CHECK_ARG(out_bytes != NULL, TAG);
    return uart_get_buffered_data_len(h->port, out_bytes);
}

esp_err_t e32p_uart_set_loopback(e32p_uart_handle_t h, bool enable)
{
    E32P_CHECK_ARG(h != NULL, TAG);
    return uart_set_loop_back(h->port, enable);
}
