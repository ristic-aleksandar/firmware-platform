/**
 * @file main.c
 * @brief 06_spi_loopback — full-duplex SPI transfer, checked against a
 *        MOSI->MISO loopback jumper.
 *
 * Proves the M3 SPI bus pattern: e32p_spi bus + device + transfer. The transfer
 * always runs (driver-level proof on a bare board); the byte-for-byte match is
 * PASS only when GPIO23 (MOSI) is jumpered to GPIO19 (MISO).
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <string.h>
#include "e32p_spi.h"

#define SPI_HOST_NUM   2
#define PIN_SCLK       18
#define PIN_MOSI       23
#define PIN_MISO       19
#define PIN_CS         21

static const char *TAG = "spi_loop";

void app_main(void)
{
    e32p_spi_bus_config_t bcfg = { .host = SPI_HOST_NUM, .sclk_pin = PIN_SCLK,
                                   .mosi_pin = PIN_MOSI, .miso_pin = PIN_MISO,
                                   .max_transfer_sz = 0 };
    e32p_spi_bus_handle_t bus = NULL;
    ESP_ERROR_CHECK(e32p_spi_bus_init(&bcfg, &bus));

    e32p_spi_dev_config_t dcfg = { .cs_pin = PIN_CS, .clock_hz = 1000000,
                                   .mode = 0, .queue_size = 3 };
    e32p_spi_dev_handle_t dev = NULL;
    ESP_ERROR_CHECK(e32p_spi_add_device(bus, &dcfg, &dev));
    ESP_LOGI(TAG, "SPI%d ready (sclk=%d mosi=%d miso=%d cs=%d) - jumper MOSI->MISO for PASS",
             SPI_HOST_NUM, PIN_SCLK, PIN_MOSI, PIN_MISO, PIN_CS);

    for (int n = 0; ; n++) {
        uint8_t tx[4] = { 0xE3, 0x2A, (uint8_t)n, 0x55 };
        uint8_t rx[4] = { 0 };

        esp_err_t err = e32p_spi_transfer(dev, tx, rx, sizeof(tx));
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "transfer failed: %s", esp_err_to_name(err));
        } else {
            bool match = (memcmp(tx, rx, sizeof(tx)) == 0);
            ESP_LOGI(TAG, "transfer ok  tx=%02x%02x%02x%02x rx=%02x%02x%02x%02x  [%s]",
                     (unsigned)tx[0], (unsigned)tx[1], (unsigned)tx[2], (unsigned)tx[3],
                     (unsigned)rx[0], (unsigned)rx[1], (unsigned)rx[2], (unsigned)rx[3],
                     match ? "PASS (loopback)" : "no jumper (rx undefined)");
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
