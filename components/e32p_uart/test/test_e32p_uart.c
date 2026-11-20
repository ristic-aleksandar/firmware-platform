/**
 * @file test_e32p_uart.c
 * @brief Unity tests for e32p_uart. Uses internal loopback so TX/RX round-trips
 *        without any wiring. Run on-target.
 */
#include "unity.h"
#include <string.h>
#include "e32p_uart.h"

#define T_PORT   1
#define T_TX     17
#define T_RX     16

TEST_CASE("e32p_uart: loopback write/read round-trip", "[e32p_uart]")
{
    e32p_uart_config_t cfg = E32P_UART_CONFIG_DEFAULT();
    cfg.port = T_PORT;  cfg.tx_pin = T_TX;  cfg.rx_pin = T_RX;

    e32p_uart_handle_t h = NULL;
    TEST_ESP_OK(e32p_uart_init(&cfg, &h));
    TEST_ESP_OK(e32p_uart_set_loopback(h, true));
    TEST_ESP_OK(e32p_uart_flush_input(h));

    const char *msg = "e32p";
    TEST_ASSERT_EQUAL_INT(4, e32p_uart_write(h, msg, 4));

    char buf[8] = {0};
    TEST_ASSERT_EQUAL_INT(4, e32p_uart_read(h, buf, 4, 200));
    TEST_ASSERT_EQUAL_STRING_LEN("e32p", buf, 4);

    TEST_ESP_OK(e32p_uart_deinit(h));
}

TEST_CASE("e32p_uart: rejects bad args", "[e32p_uart]")
{
    e32p_uart_handle_t h = NULL;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_uart_init(NULL, &h));

    e32p_uart_config_t cfg = E32P_UART_CONFIG_DEFAULT();
    cfg.port = 0;               /* console port is rejected */
    cfg.tx_pin = T_TX;  cfg.rx_pin = T_RX;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_uart_init(&cfg, &h));

    TEST_ASSERT_EQUAL_INT(-1, e32p_uart_write(NULL, "x", 1));
}
