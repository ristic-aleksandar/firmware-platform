/**
 * @file hil_sensor.c
 * @brief Device-driver HIL tests. Unlike the peripheral self-tests, these need
 *        the physical part on the wire: a BMP280 on the I2C bus and a TM1637
 *        display (pins are per-target — see hil_pins.h / tests/hil/WIRING.md).
 *        They fail cleanly if the part is absent; the host marks them opt-in
 *        (`--with-sensors`).
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "e32p_i2c.h"
#include "e32p_bmp280.h"
#include "e32p_tm1637.h"
#include "hil_agent.h"
#include "hil_sensor.h"
#include "hil_pins.h"

hil_verdict_t hil_sensor_bmp280(char *d, size_t n)
{
    e32p_i2c_bus_config_t bc = { .port = 0, .sda_pin = HIL_I2C_SDA, .scl_pin = HIL_I2C_SCL,
                                 .enable_pullups = true };
    e32p_i2c_bus_handle_t bus = NULL;
    if (e32p_i2c_bus_init(&bc, &bus) != ESP_OK) {
        snprintf(d, n, "i2c bus init failed");
        return HIL_FAIL;
    }

    e32p_bmp280_config_t cfg = E32P_BMP280_CONFIG_DEFAULT(bus);
    e32p_bmp280_handle_t s = NULL;
    esp_err_t err = e32p_bmp280_init(&cfg, &s);
    if (err != ESP_OK) {
        e32p_i2c_bus_deinit(bus);
        snprintf(d, n, "bmp280 init: %s (wired at 0x76?)", esp_err_to_name(err));
        return HIL_FAIL;
    }

    e32p_bmp280_reading_t r;
    err = e32p_bmp280_read(s, &r);
    e32p_bmp280_deinit(s);
    e32p_i2c_bus_deinit(bus);
    if (err != ESP_OK) {
        snprintf(d, n, "read: %s", esp_err_to_name(err));
        return HIL_FAIL;
    }
    /* Sea-level-ish sanity: ~300–1200 hPa and a room-ish temperature. */
    if (r.pressure_pa < 30000 || r.pressure_pa > 120000) {
        snprintf(d, n, "implausible P=%lu Pa", (unsigned long)r.pressure_pa);
        return HIL_FAIL;
    }
    long frac = r.temperature_c100 % 100; if (frac < 0) frac = -frac;
    snprintf(d, n, "T=%ld.%02ld C  P=%lu Pa",
             (long)(r.temperature_c100 / 100), frac, (unsigned long)r.pressure_pa);
    return HIL_PASS;
}

hil_verdict_t hil_sensor_tm1637(char *d, size_t n)
{
    e32p_tm1637_config_t cfg = { .clk_pin = HIL_TM1637_CLK, .dio_pin = HIL_TM1637_DIO, .brightness = 4 };
    e32p_tm1637_handle_t disp = NULL;
    esp_err_t ierr = e32p_tm1637_init(&cfg, &disp);
    if (ierr != ESP_OK) {
        snprintf(d, n, "tm1637 init: %s (display wired at clk=%d dio=%d + 3V3?)",
                 esp_err_to_name(ierr), HIL_TM1637_CLK, HIL_TM1637_DIO);
        return HIL_FAIL;
    }
    /* Slow enough for a human to read: count 0..12 (~350 ms/step), then a lit-colon
       12:05, then clear. ~5 s total, well under the host's 8 s timeout. Every write
       is ACK-checked, so a mid-run disconnect still fails the test. */
    esp_err_t werr = ESP_OK;
    for (int v = 0; v <= 12 && werr == ESP_OK; v++) {
        werr = e32p_tm1637_show_int(disp, v, false, false);
        vTaskDelay(pdMS_TO_TICKS(350));
    }
    if (werr == ESP_OK) {
        werr = e32p_tm1637_show_int(disp, 1205, true, true);   /* 12:05 */
        vTaskDelay(pdMS_TO_TICKS(700));
    }
    esp_err_t cerr = e32p_tm1637_clear(disp);
    e32p_tm1637_deinit(disp);
    if (werr != ESP_OK || cerr != ESP_OK) {
        snprintf(d, n, "write error (w=%s c=%s)", esp_err_to_name(werr), esp_err_to_name(cerr));
        return HIL_FAIL;
    }
    /* The ACK bit confirms a display is present/powered; a human confirms the
       actual digits (no way to read displayed segments back over this bus). */
    snprintf(d, n, "acked; counted 0..12 then 12:05 then cleared (visual)");
    return HIL_PASS;
}
