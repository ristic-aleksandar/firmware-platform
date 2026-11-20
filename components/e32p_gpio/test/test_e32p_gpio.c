/**
 * @file test_e32p_gpio.c
 * @brief Unity tests for e32p_gpio: output round-trip + the WROOM-32 guard paths.
 *        Run on-target (source of truth per plan).
 *
 * GPIO4 is used for the round-trip case: free on the bench, not a strapping pin,
 * and unloaded on both DUT boards used so far. GPIO2 was the first choice and is
 * wrong on the current DUT board, which pulls it down: with only the internal
 * pull-up it reads 0 and never rises (measured 2026-10-05; no LED is on it).
 */
#include "unity.h"
#include "esp_rom_sys.h"
#include "e32p_gpio.h"

#define TEST_PIN_IO   4
#define OD_SETTLE_US  100  /* a released open-drain line rises through the ~45 kOhm
                              internal pull-up (about 2 us on GPIO4): wait, don't race it */

TEST_CASE("e32p_gpio: output set/get round-trip", "[e32p_gpio]")
{
    e32p_gpio_config_t cfg = E32P_GPIO_CONFIG_DEFAULT();
    cfg.pin  = TEST_PIN_IO;
    cfg.mode = E32P_GPIO_MODE_INPUT_OUTPUT_OD;
    cfg.pull = E32P_GPIO_PULL_UP;
    TEST_ESP_OK(e32p_gpio_config(&cfg));

    int level = -1;
    TEST_ESP_OK(e32p_gpio_set_level(TEST_PIN_IO, 1));
    esp_rom_delay_us(OD_SETTLE_US);
    TEST_ESP_OK(e32p_gpio_get_level(TEST_PIN_IO, &level));
    TEST_ASSERT_EQUAL_INT(1, level);

    TEST_ESP_OK(e32p_gpio_reset(TEST_PIN_IO));
}

TEST_CASE("e32p_gpio: NULL config rejected", "[e32p_gpio]")
{
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_gpio_config(NULL));
}

TEST_CASE("e32p_gpio: input-only pin cannot be output", "[e32p_gpio]")
{
    e32p_gpio_config_t cfg = E32P_GPIO_CONFIG_DEFAULT();
    cfg.pin  = 34;                       /* GPIO34-39 are input-only on ESP32 */
    cfg.mode = E32P_GPIO_MODE_OUTPUT;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_gpio_config(&cfg));
}

#if CONFIG_E32P_PIN_GUARDS && CONFIG_IDF_TARGET_ESP32
TEST_CASE("e32p_gpio: SPI-flash pin refused by guards", "[e32p_gpio]")
{
    e32p_gpio_config_t cfg = E32P_GPIO_CONFIG_DEFAULT();
    cfg.pin  = 7;                        /* GPIO6-11 wired to the SPI flash */
    cfg.mode = E32P_GPIO_MODE_OUTPUT;
    TEST_ASSERT_EQUAL(ESP_ERR_E32P_PIN_RESERVED, e32p_gpio_config(&cfg));
}
#endif
