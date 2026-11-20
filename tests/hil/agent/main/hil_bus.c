/**
 * @file hil_bus.c
 * @brief Bus HIL tests + the UART echo companion.
 */
#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "e32p_uart.h"
#include "e32p_i2c.h"
#include "e32p_spi.h"
#include "e32p_i2s.h"
#include "e32p_ws2812.h"
#include "hil_agent.h"
#include "hil_bus.h"
#include "hil_pins.h"

#define UART_PORT   1
#define UART_TX     HIL_UART1_TX
#define UART_RX     HIL_UART1_RX

hil_verdict_t hil_bus_uart_loop(char *d, size_t n)
{
    /* Internal loopback uses dedicated, unwired pins (not the UART1 cross-wire
       pins) so that when the two-board bus_uart_xfer cross-wire is connected, the
       companion's echo can't overlay this on-chip loopback and corrupt it. */
    e32p_uart_config_t cfg = E32P_UART_CONFIG_DEFAULT();
    cfg.port = UART_PORT; cfg.tx_pin = HIL_UART_LOOP_TX; cfg.rx_pin = HIL_UART_LOOP_RX;
    e32p_uart_handle_t h = NULL;
    if (e32p_uart_init(&cfg, &h) != ESP_OK) {
        snprintf(d, n, "uart init failed");
        return HIL_FAIL;
    }
    e32p_uart_set_loopback(h, true);
    const char *msg = "HILUART";
    e32p_uart_write(h, msg, 7);
    char buf[16] = {0};
    int r = e32p_uart_read(h, buf, 7, 200);
    e32p_uart_deinit(h);
    if (r != 7 || memcmp(buf, msg, 7) != 0) {
        snprintf(d, n, "internal loopback r=%d", r);
        return HIL_FAIL;
    }
    snprintf(d, n, "internal loopback 7B ok");
    return HIL_PASS;
}

hil_verdict_t hil_bus_uart_xfer(char *d, size_t n)
{
    e32p_uart_config_t cfg = E32P_UART_CONFIG_DEFAULT();
    cfg.port = UART_PORT; cfg.tx_pin = UART_TX; cfg.rx_pin = UART_RX;
    e32p_uart_handle_t h = NULL;
    if (e32p_uart_init(&cfg, &h) != ESP_OK) {
        snprintf(d, n, "uart init failed");
        return HIL_FAIL;
    }
    /* The very first exchange after an idle stretch can read a line-settle glitch
       byte — while UART1 is torn down between runs the DUT's TX pin floats, so the
       companion picks up a stray byte and echoes it. The link is solid once warmed
       up, so retry: drain any in-flight echo, then send/receive again. */
    const char *msg = "PING1";
    char buf[16] = {0};
    int r = 0;
    for (int attempt = 0; attempt < 4; attempt++) {
        vTaskDelay(pdMS_TO_TICKS(30));   /* let any in-flight echo arrive... */
        e32p_uart_flush_input(h);        /* ...then discard stale/glitch bytes */
        memset(buf, 0, sizeof(buf));
        e32p_uart_write(h, msg, 5);
        r = e32p_uart_read(h, buf, 5, 400);  /* companion echoes it back */
        if (r == 5 && memcmp(buf, msg, 5) == 0) {
            e32p_uart_deinit(h);
            snprintf(d, n, "uart echo round-trip ok (try %d)", attempt + 1);
            return HIL_PASS;
        }
    }
    e32p_uart_deinit(h);
    snprintf(d, n, "echo r=%d got=[%02x %02x %02x %02x %02x] want 'PING1' (cross-wire?)",
             r, (uint8_t)buf[0], (uint8_t)buf[1], (uint8_t)buf[2],
             (uint8_t)buf[3], (uint8_t)buf[4]);
    return HIL_FAIL;
}

hil_verdict_t hil_bus_i2c_bus(char *d, size_t n)
{
    e32p_i2c_bus_config_t cfg = { .port = 0, .sda_pin = HIL_I2C_SDA, .scl_pin = HIL_I2C_SCL,
                                  .enable_pullups = true };
    e32p_i2c_bus_handle_t bus = NULL;
    if (e32p_i2c_bus_init(&cfg, &bus) != ESP_OK) {
        snprintf(d, n, "bus init failed");
        return HIL_FAIL;
    }
    uint8_t found[8];
    size_t cnt = 0;
    esp_err_t err = e32p_i2c_scan(bus, found, 8, &cnt);
    e32p_i2c_bus_deinit(bus);
    if (err != ESP_OK) {
        snprintf(d, n, "scan=%s", esp_err_to_name(err));
        return HIL_FAIL;
    }
    snprintf(d, n, "bus ok, %u device(s) on the bus", (unsigned)cnt);
    return HIL_PASS;
}

hil_verdict_t hil_bus_spi_loop(char *d, size_t n)
{
    e32p_spi_bus_config_t bcfg = { .host = 2, .sclk_pin = HIL_SPI_SCLK, .mosi_pin = HIL_SPI_MOSI,
                                   .miso_pin = HIL_SPI_MISO, .max_transfer_sz = 0 };
    e32p_spi_bus_handle_t bus = NULL;
    if (e32p_spi_bus_init(&bcfg, &bus) != ESP_OK) {
        snprintf(d, n, "bus init failed");
        return HIL_FAIL;
    }
    e32p_spi_dev_config_t dcfg = { .cs_pin = HIL_SPI_CS, .clock_hz = 1000000, .mode = 0, .queue_size = 1 };
    e32p_spi_dev_handle_t dev = NULL;
    if (e32p_spi_add_device(bus, &dcfg, &dev) != ESP_OK) {
        e32p_spi_bus_deinit(bus);
        snprintf(d, n, "add device failed");
        return HIL_FAIL;
    }
    uint8_t tx[4] = { 0xDE, 0xAD, 0xBE, 0xEF };
    uint8_t rx[4] = {0};
    esp_err_t err = e32p_spi_transfer(dev, tx, rx, 4);
    e32p_spi_remove_device(dev);
    e32p_spi_bus_deinit(bus);
    if (err != ESP_OK) {
        snprintf(d, n, "transfer=%s", esp_err_to_name(err));
        return HIL_FAIL;
    }
    snprintf(d, n, "bus+device+transfer ok (byte-match needs MOSI-MISO jumper)");
    return HIL_PASS;
}

hil_verdict_t hil_bus_i2s_tx(char *d, size_t n)
{
    e32p_i2s_config_t cfg = { .port = 0, .dir = E32P_I2S_TX, .bclk_gpio = HIL_I2S_BCLK,
                              .ws_gpio = HIL_I2S_WS, .data_gpio = HIL_I2S_DATA,
                              .sample_rate = 16000, .bits = 16, .stereo = false };
    e32p_i2s_handle_t h = NULL;
    if (e32p_i2s_init(&cfg, &h) != ESP_OK) {
        snprintf(d, n, "i2s init failed");
        return HIL_FAIL;
    }
    int16_t samples[64] = {0};
    size_t written = 0;
    esp_err_t err = e32p_i2s_write(h, samples, sizeof(samples), &written, 500);
    e32p_i2s_deinit(h);
    if (err != ESP_OK || written == 0) {
        snprintf(d, n, "write=%s w=%u", esp_err_to_name(err), (unsigned)written);
        return HIL_FAIL;
    }
    snprintf(d, n, "i2s TX wrote %u bytes", (unsigned)written);
    return HIL_PASS;
}

hil_verdict_t hil_bus_ws2812(char *d, size_t n)
{
    e32p_ws2812_handle_t h = NULL;
    if (e32p_ws2812_init(HIL_WS2812_DIN, 8, &h) != ESP_OK) {
        snprintf(d, n, "ws2812 init failed");
        return HIL_FAIL;
    }
    e32p_ws2812_clear(h);
    e32p_ws2812_set_pixel(h, 0, 32, 0, 0);
    esp_err_t err = e32p_ws2812_refresh(h);
    e32p_ws2812_deinit(h);
    if (err != ESP_OK) {
        snprintf(d, n, "refresh=%s", esp_err_to_name(err));
        return HIL_FAIL;
    }
    snprintf(d, n, "ws2812 refresh 8 leds ok");
    return HIL_PASS;
}

/* --- UART echo companion --- */

static void uart_companion_task(void *arg)
{
    (void)arg;
    e32p_uart_config_t cfg = E32P_UART_CONFIG_DEFAULT();
    cfg.port = UART_PORT; cfg.tx_pin = UART_TX; cfg.rx_pin = UART_RX;
    e32p_uart_handle_t h = NULL;
    if (e32p_uart_init(&cfg, &h) != ESP_OK) {
        hil_emit("@EVT UART_ERR init failed");
        vTaskDelete(NULL);
        return;
    }
    hil_emit("@EVT COMPANION uart echo ready (UART1 %d/%d)", UART_TX, UART_RX);
    char buf[64];
    for (;;) {
        int r = e32p_uart_read(h, buf, sizeof(buf), 100);
        if (r > 0) {
            e32p_uart_write(h, buf, r);  /* echo back */
        }
    }
}

void hil_bus_start_companion(void)
{
    xTaskCreate(uart_companion_task, "hil_uart_comp", 4096, NULL, 5, NULL);
}
