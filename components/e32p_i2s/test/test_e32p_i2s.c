/**
 * @file test_e32p_i2s.c
 * @brief Unity tests for e32p_i2s. Headless-safe: an I2S master generates its own
 *        clock, so a TX write drains through the DMA without an external DAC.
 */
#include "unity.h"
#include "e32p_i2s.h"

TEST_CASE("e32p_i2s: TX init / write / deinit", "[e32p_i2s]")
{
    e32p_i2s_config_t cfg = { .port = 0, .dir = E32P_I2S_TX, .bclk_gpio = 26, .ws_gpio = 25,
                              .data_gpio = 22, .sample_rate = 16000, .bits = 16, .stereo = false };
    e32p_i2s_handle_t h = NULL;
    TEST_ESP_OK(e32p_i2s_init(&cfg, &h));

    int16_t buf[64] = {0};
    size_t written = 0;
    TEST_ESP_OK(e32p_i2s_write(h, buf, sizeof(buf), &written, 500));
    TEST_ASSERT_TRUE(written > 0);

    TEST_ESP_OK(e32p_i2s_deinit(h));
}

TEST_CASE("e32p_i2s: rejects bad args", "[e32p_i2s]")
{
    e32p_i2s_handle_t h = NULL;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_i2s_init(NULL, &h));

    e32p_i2s_config_t cfg = { .port = 0, .dir = E32P_I2S_TX, .bclk_gpio = 26, .ws_gpio = 25,
                              .data_gpio = 22, .sample_rate = 16000, .bits = 8,  /* unsupported */
                              .stereo = false };
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_i2s_init(&cfg, &h));
}
