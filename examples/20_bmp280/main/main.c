/**
 * @file main.c
 * @brief 20_bmp280 — read temperature + pressure from a BMP280 over e32p_i2c.
 *
 * Two parts:
 *   1. Runs the pure Bosch compensation on the datasheet reference vector, so the
 *      math is demonstrated on-target even with no sensor attached.
 *   2. Attempts a live sensor: if a BMP280 answers on the bus it samples once a
 *      second; otherwise it explains the wiring and idles.
 *
 * Wiring: SDA=GPIO13, SCL=GPIO14, VCC=3V3, SDO=GND (addr 0x76), CSB=3V3.
 * (GPIO13/14 keeps I2C off the 21/22 TWAI pins, matching the HIL bench rig.)
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "e32p_i2c.h"
#include "e32p_bmp280.h"

static const char *TAG = "bmp280_demo";

static void demo_reference_vector(void)
{
    /* Bosch datasheet reference calibration + raw ADC codes -> known outputs. */
    const e32p_bmp280_calib_t ref = {
        .dig_t1 = 27504, .dig_t2 = 26435, .dig_t3 = -1000,
        .dig_p1 = 36477, .dig_p2 = -10685, .dig_p3 = 3024, .dig_p4 = 2855, .dig_p5 = 140,
        .dig_p6 = -7, .dig_p7 = 15500, .dig_p8 = -14600, .dig_p9 = 6000,
    };
    int32_t t_fine = 0;
    int32_t t = e32p_bmp280_compensate_temp(519888, &ref, &t_fine);
    uint32_t p = e32p_bmp280_compensate_press(415148, &ref, t_fine);
    ESP_LOGI(TAG, "reference vector (no sensor): T=%ld.%02ld C  P=%lu Pa  (expect 25.08 C / 100653 Pa)",
             (long)(t / 100), (long)(t % 100), (unsigned long)p);
}

void app_main(void)
{
    demo_reference_vector();

    e32p_i2c_bus_config_t bc = { .port = 0, .sda_pin = 13, .scl_pin = 14, .enable_pullups = true };
    e32p_i2c_bus_handle_t bus = NULL;
    ESP_ERROR_CHECK(e32p_i2c_bus_init(&bc, &bus));

    e32p_bmp280_config_t cfg = E32P_BMP280_CONFIG_DEFAULT(bus);
    e32p_bmp280_handle_t sensor = NULL;
    esp_err_t err = e32p_bmp280_init(&cfg, &sensor);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "no BMP280 found (%s). Wire it at 0x76: SDA=13 SCL=14 VCC=3V3, then reset.",
                 esp_err_to_name(err));
        ESP_LOGW(TAG, "idling; the pure compensation above already ran on-target.");
        return;
    }

    ESP_LOGI(TAG, "BMP280 online; sampling every second.");
    while (1) {
        e32p_bmp280_reading_t r;
        if (e32p_bmp280_read(sensor, &r) == ESP_OK) {
            long frac = r.temperature_c100 % 100;
            if (frac < 0) frac = -frac;
            ESP_LOGI(TAG, "T=%ld.%02ld C   P=%lu Pa   (%lu.%02lu hPa)",
                     (long)(r.temperature_c100 / 100), frac,
                     (unsigned long)r.pressure_pa,
                     (unsigned long)(r.pressure_pa / 100), (unsigned long)(r.pressure_pa % 100));
        } else {
            ESP_LOGW(TAG, "read failed");
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
