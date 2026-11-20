/**
 * @file hil_modbus.c
 * @brief Modbus HIL tests + the Modbus-slave companion.
 *
 *   modbus_selftest  — single board, no wire: CRC vector + a master<->slave
 *                      register round-trip through the pure protocol + slave
 *                      handler (in memory).
 *   modbus_rtu_xfer  — two boards: the DUT drives the real e32p_modbus UART
 *                      master against a slave the COMPANION runs on a crossed
 *                      UART2 link. Proves the RTU transport (framing, CRC on the
 *                      wire, inter-frame timing, exception replies), not just the
 *                      pure protocol.
 */
#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "e32p_modbus.h"
#include "e32p_uart.h"
#include "hil_agent.h"
#include "hil_modbus.h"
#include "hil_pins.h"

/* Two-board RTU link: UART2, crossed, 9600 8E1 — the e32p_modbus master defaults.
   Pins are per-target (hil_pins.h); UART1 stays free for the echo peer, so a
   companion can run both at once. */
#define MB_UART_PORT   2
#define MB_TX          HIL_MODBUS_TX
#define MB_RX          HIL_MODBUS_RX
#define MB_SLAVE_ADDR  1

hil_verdict_t hil_modbus_selftest(char *d, size_t n)
{
    if (e32p_modbus_crc16((const uint8_t *)"123456789", 9) != 0x4B37) {
        snprintf(d, n, "crc16 vector wrong");
        return HIL_FAIL;
    }

    uint16_t holding[4] = { 111, 222, 333, 444 };
    e32p_modbus_slave_t slave = { .address = 1, .holding = holding, .n_holding = 4 };
    uint8_t req[32], resp[64];
    uint16_t out[4];
    size_t count = 0;

    /* Master reads holding[1..2] from the in-memory slave. */
    int rl = e32p_modbus_build_read(req, sizeof(req), 1, E32P_MODBUS_FC_READ_HOLDING, 1, 2);
    int sl = e32p_modbus_slave_process(&slave, req, rl, resp, sizeof(resp));
    if (sl <= 0 ||
        e32p_modbus_parse_read_response(resp, sl, 1, E32P_MODBUS_FC_READ_HOLDING, out, 4, &count, NULL) != ESP_OK ||
        count != 2 || out[0] != 222 || out[1] != 333) {
        snprintf(d, n, "read round-trip failed");
        return HIL_FAIL;
    }

    /* Master writes holding[0] = 4242 and the slave applies it. */
    rl = e32p_modbus_build_write_single(req, sizeof(req), 1, 0, 4242);
    sl = e32p_modbus_slave_process(&slave, req, rl, resp, sizeof(resp));
    if (sl <= 0 || holding[0] != 4242) {
        snprintf(d, n, "write round-trip failed");
        return HIL_FAIL;
    }

    /* Out-of-range read returns an illegal-data-address exception. */
    uint8_t exc = 0;
    rl = e32p_modbus_build_read(req, sizeof(req), 1, E32P_MODBUS_FC_READ_HOLDING, 100, 1);
    sl = e32p_modbus_slave_process(&slave, req, rl, resp, sizeof(resp));
    if (e32p_modbus_parse_read_response(resp, sl, 1, E32P_MODBUS_FC_READ_HOLDING, out, 4, NULL, &exc)
            != ESP_ERR_INVALID_RESPONSE || exc != E32P_MODBUS_EXC_ILLEGAL_DATA_ADDRESS) {
        snprintf(d, n, "exception path failed (exc=0x%02x)", exc);
        return HIL_FAIL;
    }

    snprintf(d, n, "crc + read/write/exception round-trip ok");
    return HIL_PASS;
}

/* --- Two-board RTU transport (DUT side) ------------------------------------ */

hil_verdict_t hil_modbus_rtu_xfer(char *d, size_t n)
{
    e32p_modbus_master_config_t cfg = E32P_MODBUS_MASTER_CONFIG_DEFAULT();
    cfg.uart_port = MB_UART_PORT;
    cfg.tx_pin = MB_TX;
    cfg.rx_pin = MB_RX;
    e32p_modbus_master_handle_t m = NULL;
    if (e32p_modbus_master_init(&cfg, &m) != ESP_OK) {
        snprintf(d, n, "master init failed");
        return HIL_FAIL;
    }

    /* Read all four holding registers over the wire. regs[1..3] are the slave's
       fixed pattern (never written here), so the check stays valid on re-runs. */
    uint16_t regs[4] = { 0 };
    esp_err_t err = e32p_modbus_read_holding_registers(m, MB_SLAVE_ADDR, 0, 4, regs);
    if (err != ESP_OK) {
        snprintf(d, n, "read4=%s (need companion + cross-wire UART2 %d/%d)",
                 esp_err_to_name(err), MB_TX, MB_RX);
        e32p_modbus_master_deinit(m);
        return HIL_FAIL;
    }
    if (regs[1] != 2000 || regs[2] != 3000 || regs[3] != 4000) {
        snprintf(d, n, "read mismatch [%u,%u,%u,%u]", regs[0], regs[1], regs[2], regs[3]);
        e32p_modbus_master_deinit(m);
        return HIL_FAIL;
    }

    /* Write holding[0] and read it back over the wire. */
    err = e32p_modbus_write_single_register(m, MB_SLAVE_ADDR, 0, 4242);
    uint16_t rb = 0;
    if (err == ESP_OK) {
        err = e32p_modbus_read_holding_registers(m, MB_SLAVE_ADDR, 0, 1, &rb);
    }
    if (err != ESP_OK || rb != 4242) {
        snprintf(d, n, "write/readback err=%s rb=%u", esp_err_to_name(err), rb);
        e32p_modbus_master_deinit(m);
        return HIL_FAIL;
    }

    /* An out-of-range read must come back as a Modbus exception on the wire. */
    uint16_t dummy = 0;
    err = e32p_modbus_read_holding_registers(m, MB_SLAVE_ADDR, 100, 1, &dummy);
    uint8_t exc = e32p_modbus_last_exception(m);
    e32p_modbus_master_deinit(m);
    if (err != ESP_ERR_INVALID_RESPONSE || exc != E32P_MODBUS_EXC_ILLEGAL_DATA_ADDRESS) {
        snprintf(d, n, "exception path err=%s exc=0x%02x", esp_err_to_name(err), exc);
        return HIL_FAIL;
    }

    snprintf(d, n, "read4 + write/readback + exception over UART2 ok");
    return HIL_PASS;
}

/* --- Modbus-slave companion (COMPANION side) ------------------------------- */

static void modbus_companion_task(void *arg)
{
    (void)arg;
    static uint16_t holding[4] = { 1000, 2000, 3000, 4000 };
    e32p_modbus_slave_t slave = { .address = MB_SLAVE_ADDR, .holding = holding, .n_holding = 4 };

    e32p_uart_config_t ucfg = E32P_UART_CONFIG_DEFAULT();
    ucfg.port = MB_UART_PORT;
    ucfg.tx_pin = MB_TX;
    ucfg.rx_pin = MB_RX;
    ucfg.baud_rate = 9600;
    ucfg.parity = E32P_UART_PARITY_EVEN;   /* match the master default (8E1) */
    e32p_uart_handle_t h = NULL;
    if (e32p_uart_init(&ucfg, &h) != ESP_OK) {
        hil_emit("@EVT MODBUS_ERR init failed");
        vTaskDelete(NULL);
        return;
    }
    hil_emit("@EVT COMPANION modbus slave ready (UART2 %d/%d addr=%d)", MB_TX, MB_RX, MB_SLAVE_ADDR);

    uint8_t adu[256];
    uint8_t resp[256];
    size_t len = 0;
    for (;;) {
        /* Accumulate a request burst; a read that times out (20 ms >> t3.5 at
           9600) marks the frame boundary. */
        int r = e32p_uart_read(h, adu + len, sizeof(adu) - len, 20);
        if (r > 0) {
            len += (size_t)r;
            if (len >= sizeof(adu)) {
                len = 0;   /* overrun — drop and resync */
            }
            continue;
        }
        if (len >= 4) {    /* smallest RTU frame is addr+func+CRC16 = 4 bytes */
            int rl = e32p_modbus_slave_process(&slave, adu, len, resp, sizeof(resp));
            if (rl > 0) {
                e32p_uart_write(h, resp, (size_t)rl);
            }
        }
        len = 0;
    }
}

void hil_modbus_start_companion(void)
{
    xTaskCreate(modbus_companion_task, "hil_mb_comp", 4096, NULL, 5, NULL);
}
