/**
 * @file main.c
 * @brief 24_modbus — a Modbus RTU master and an in-firmware slave on one board,
 *        talking over two crossed UARTs. Proves the whole stack (framing, CRC,
 *        UART transport, master + slave) with no RS-485 hardware — just 2 wires.
 *
 * Wiring (cross-connect the two UARTs):
 *   master UART1 TX = GPIO17  --->  slave  UART2 RX = GPIO5
 *   slave  UART2 TX = GPIO4   --->  master UART1 RX = GPIO16
 *
 * Each cycle the master writes an incrementing value to holding[0] and reads
 * holding[0..3] back from the slave, printing them. If you see the values, the
 * full request/response round-trip is working over real serial lines.
 */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "e32p_uart.h"
#include "e32p_modbus.h"

static const char *TAG = "modbus_demo";

/* The slave's register bank; the slave task owns it, the master reaches it only
   over the wire. */
static uint16_t s_holding[8] = { 100, 200, 300, 400, 0, 0, 0, 0 };
static e32p_uart_handle_t s_slave_uart;

/* Minimal Modbus slave: read a frame, answer it, repeat. */
static void slave_task(void *arg)
{
    (void)arg;
    e32p_modbus_slave_t model = { .address = 1, .holding = s_holding, .n_holding = 8 };
    uint8_t buf[E32P_MODBUS_ADU_MAX];
    size_t len = 0;

    for (;;) {
        int n = e32p_uart_read(s_slave_uart, buf + len, sizeof(buf) - len, 50);
        if (n > 0) {
            len += (size_t)n;
            if (len >= 4 && e32p_modbus_adu_check(buf, len)) {
                uint8_t resp[E32P_MODBUS_ADU_MAX];
                int rl = e32p_modbus_slave_process(&model, buf, len, resp, sizeof(resp));
                if (rl > 0) {
                    e32p_uart_write(s_slave_uart, resp, (size_t)rl);
                }
                len = 0;
            } else if (len >= sizeof(buf)) {
                len = 0;   /* overflow: resync */
            }
        } else if (len > 0) {
            len = 0;       /* idle gap with a partial frame: discard and resync */
        }
    }
}

void app_main(void)
{
    /* Slave on UART2 (TX=4, RX=5), 9600 8N1. */
    e32p_uart_config_t su = E32P_UART_CONFIG_DEFAULT();
    su.port = 2; su.tx_pin = 4; su.rx_pin = 5; su.baud_rate = 9600;
    su.parity = E32P_UART_PARITY_NONE;
    ESP_ERROR_CHECK(e32p_uart_init(&su, &s_slave_uart));
    xTaskCreate(slave_task, "mb_slave", 4096, NULL, 5, NULL);

    /* Master on UART1 (TX=17, RX=16), matching 9600 8N1. */
    e32p_modbus_master_config_t mc = E32P_MODBUS_MASTER_CONFIG_DEFAULT();
    mc.parity = E32P_UART_PARITY_NONE;
    e32p_modbus_master_handle_t mb;
    ESP_ERROR_CHECK(e32p_modbus_master_init(&mc, &mb));

    ESP_LOGI(TAG, "cross-wire UART1 TX17->UART2 RX5 and UART2 TX4->UART1 RX16");

    int cycle = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(3000));

        esp_err_t err = e32p_modbus_write_single_register(mb, 1, 0, (uint16_t)(1000 + cycle));
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "write failed: %s (exc 0x%02x) — check the cross-wiring",
                     esp_err_to_name(err), e32p_modbus_last_exception(mb));
            cycle++;
            continue;
        }

        uint16_t regs[4] = { 0 };
        err = e32p_modbus_read_holding_registers(mb, 1, 0, 4, regs);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "cycle %d: holding[0..3] = %u %u %u %u",
                     cycle, regs[0], regs[1], regs[2], regs[3]);
        } else {
            ESP_LOGW(TAG, "read failed: %s (exc 0x%02x)",
                     esp_err_to_name(err), e32p_modbus_last_exception(mb));
        }
        cycle++;
    }
}
