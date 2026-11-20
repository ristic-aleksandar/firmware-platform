#pragma once
/**
 * @file hil_bus.h
 * @brief Bus HIL tests: uart, i2c, spi, i2s, ws2812. Single-board where possible;
 *        uart has a two-board variant using the UART echo companion.
 */
#include "hil_agent.h"

#ifdef __cplusplus
extern "C" {
#endif

hil_verdict_t hil_bus_uart_loop(char *detail, size_t n);   /* 1 board (internal loopback) */
hil_verdict_t hil_bus_uart_xfer(char *detail, size_t n);   /* 2 board (companion echo)     */
hil_verdict_t hil_bus_i2c_bus(char *detail, size_t n);     /* 1 board (bus init + scan)    */
hil_verdict_t hil_bus_spi_loop(char *detail, size_t n);    /* 1 board (bus+device+transfer)*/
hil_verdict_t hil_bus_i2s_tx(char *detail, size_t n);      /* 1 board (TX write)           */
hil_verdict_t hil_bus_ws2812(char *detail, size_t n);      /* 1 board (refresh)            */

/** @brief Start the UART echo peer (COMPANION) on UART1 (TX=17, RX=16). */
void hil_bus_start_companion(void);

#ifdef __cplusplus
}
#endif
