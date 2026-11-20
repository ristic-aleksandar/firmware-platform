/**
 * @file main.c
 * @brief 04_uart_loopback — sends a message on UART1 and reads it back through
 *        the on-chip loopback, so it self-tests with no wiring.
 *
 * Proves the M3 bus pattern: e32p_uart's opaque-handle-per-port lifecycle plus
 * real TX/RX. Each line prints sent vs. received with a PASS/FAIL match.
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>
#include "e32p_uart.h"

#define UART_PORT   1
#define UART_TX     17
#define UART_RX     16

static const char *TAG = "uart_loop";

void app_main(void)
{
    e32p_uart_config_t cfg = E32P_UART_CONFIG_DEFAULT();
    cfg.port = UART_PORT;  cfg.tx_pin = UART_TX;  cfg.rx_pin = UART_RX;

    e32p_uart_handle_t uart = NULL;
    ESP_ERROR_CHECK(e32p_uart_init(&cfg, &uart));
    ESP_ERROR_CHECK(e32p_uart_set_loopback(uart, true));   /* internal TX->RX */
    ESP_LOGI(TAG, "UART%d loopback ready (tx=%d rx=%d)", UART_PORT, UART_TX, UART_RX);

    char tx[32];
    char rx[32];
    for (int n = 0; ; n++) {
        int len = snprintf(tx, sizeof(tx), "e32p uart #%d", n);

        e32p_uart_flush_input(uart);
        int w = e32p_uart_write(uart, tx, len);
        int r = e32p_uart_read(uart, rx, len, 100);
        rx[(r > 0) ? r : 0] = '\0';

        bool pass = (r == w) && (memcmp(tx, rx, (w > 0) ? (size_t)w : 0) == 0);
        ESP_LOGI(TAG, "sent \"%s\" (%d) -> got \"%s\" (%d)  [%s]",
                 tx, w, rx, r, pass ? "PASS" : "FAIL");

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
