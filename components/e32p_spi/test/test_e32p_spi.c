/**
 * @file test_e32p_spi.c
 * @brief Unity tests for e32p_spi. The transfer runs on a bare board (received
 *        data is undefined without a MOSI->MISO jumper). Run on-target.
 */
#include "unity.h"
#include "e32p_spi.h"

#define T_HOST   2
#define T_SCLK   18
#define T_MOSI   23
#define T_MISO   19
#define T_CS     21

TEST_CASE("e32p_spi: bus + device lifecycle and a transfer", "[e32p_spi]")
{
    e32p_spi_bus_config_t bcfg = { .host = T_HOST, .sclk_pin = T_SCLK,
                                   .mosi_pin = T_MOSI, .miso_pin = T_MISO,
                                   .max_transfer_sz = 0 };
    e32p_spi_bus_handle_t bus = NULL;
    TEST_ESP_OK(e32p_spi_bus_init(&bcfg, &bus));
    TEST_ASSERT_NOT_NULL(bus);

    e32p_spi_dev_config_t dcfg = { .cs_pin = T_CS, .clock_hz = 1000000,
                                   .mode = 0, .queue_size = 3 };
    e32p_spi_dev_handle_t dev = NULL;
    TEST_ESP_OK(e32p_spi_add_device(bus, &dcfg, &dev));
    TEST_ASSERT_NOT_NULL(dev);

    uint8_t tx[4] = { 0xDE, 0xAD, 0xBE, 0xEF };
    uint8_t rx[4] = { 0 };
    TEST_ESP_OK(e32p_spi_transfer(dev, tx, rx, sizeof(tx)));   /* runs; rx undefined w/o jumper */

    TEST_ESP_OK(e32p_spi_remove_device(dev));
    TEST_ESP_OK(e32p_spi_bus_deinit(bus));
}

TEST_CASE("e32p_spi: rejects bad args", "[e32p_spi]")
{
    e32p_spi_bus_handle_t bus = NULL;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_spi_bus_init(NULL, &bus));

    e32p_spi_bus_config_t bcfg = { .host = 5, .sclk_pin = T_SCLK,
                                   .mosi_pin = T_MOSI, .miso_pin = T_MISO };
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_spi_bus_init(&bcfg, &bus));
}
