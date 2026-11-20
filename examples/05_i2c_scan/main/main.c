/**
 * @file main.c
 * @brief 05_i2c_scan — scans the I2C bus every 2 s and lists any devices found.
 *
 * Proves e32p_i2c bus init + probe/scan. On a bare board it reports "no devices"
 * cleanly; wire a sensor to SDA/SCL and its address appears.
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <stdio.h>
#include "e32p_i2c.h"

#define I2C_PORT   0
#define I2C_SDA    21
#define I2C_SCL    22

static const char *TAG = "i2c_scan";

void app_main(void)
{
    e32p_i2c_bus_config_t cfg = { .port = I2C_PORT, .sda_pin = I2C_SDA,
                                  .scl_pin = I2C_SCL, .enable_pullups = true };
    e32p_i2c_bus_handle_t bus = NULL;
    ESP_ERROR_CHECK(e32p_i2c_bus_init(&cfg, &bus));
    ESP_LOGI(TAG, "scanning I2C%d on SDA=%d SCL=%d ...", I2C_PORT, I2C_SDA, I2C_SCL);

    while (1) {
        uint8_t found[16];
        size_t count = 0;
        ESP_ERROR_CHECK(e32p_i2c_scan(bus, found, sizeof(found), &count));

        if (count == 0) {
            ESP_LOGI(TAG, "no I2C devices found (wire a sensor to SDA=%d SCL=%d)", I2C_SDA, I2C_SCL);
        } else {
            char line[80];
            int off = 0;
            for (size_t i = 0; i < count && i < sizeof(found); i++) {
                off += snprintf(line + off, sizeof(line) - off, " 0x%02x", (unsigned)found[i]);
            }
            ESP_LOGI(TAG, "found %u device(s):%s", (unsigned)count, line);
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
