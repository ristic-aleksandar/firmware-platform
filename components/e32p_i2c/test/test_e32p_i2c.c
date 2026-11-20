/**
 * @file test_e32p_i2c.c
 * @brief Unity tests for e32p_i2c. Runs on a bare board (no device required).
 *        Run on-target.
 */
#include "unity.h"
#include "e32p_i2c.h"

#define T_PORT   0
#define T_SDA    21
#define T_SCL    22

static e32p_i2c_bus_config_t default_cfg(void)
{
    e32p_i2c_bus_config_t cfg = { .port = T_PORT, .sda_pin = T_SDA,
                                  .scl_pin = T_SCL, .enable_pullups = true };
    return cfg;
}

TEST_CASE("e32p_i2c: bus init / deinit", "[e32p_i2c]")
{
    e32p_i2c_bus_config_t cfg = default_cfg();
    e32p_i2c_bus_handle_t bus = NULL;
    TEST_ESP_OK(e32p_i2c_bus_init(&cfg, &bus));
    TEST_ASSERT_NOT_NULL(bus);
    TEST_ESP_OK(e32p_i2c_bus_deinit(bus));
}

TEST_CASE("e32p_i2c: scan runs and probe returns a valid code", "[e32p_i2c]")
{
    e32p_i2c_bus_config_t cfg = default_cfg();
    e32p_i2c_bus_handle_t bus = NULL;
    TEST_ESP_OK(e32p_i2c_bus_init(&cfg, &bus));

    uint8_t found[8];
    size_t count = 0;
    TEST_ESP_OK(e32p_i2c_scan(bus, found, sizeof(found), &count));   /* 0 on a bare board */

    esp_err_t err = e32p_i2c_probe(bus, 0x50, 50);
    TEST_ASSERT_TRUE(err == ESP_ERR_NOT_FOUND || err == ESP_OK || err == ESP_ERR_TIMEOUT);

    TEST_ESP_OK(e32p_i2c_bus_deinit(bus));
}

TEST_CASE("e32p_i2c: NULL args rejected", "[e32p_i2c]")
{
    e32p_i2c_bus_handle_t bus = NULL;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_i2c_bus_init(NULL, &bus));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_i2c_probe(NULL, 0x50, 50));
}
