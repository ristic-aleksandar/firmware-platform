/**
 * @file test_e32p_ledc.c
 * @brief Unity tests for e32p_ledc: opaque-handle lifecycle, duty clamping,
 *        and argument validation. Run on-target (source of truth per plan).
 */
#include "unity.h"
#include "e32p_ledc.h"

#define TEST_PIN   18   /* output-capable, non-strapping */

TEST_CASE("e32p_ledc: init/start/stop/deinit lifecycle", "[e32p_ledc]")
{
    e32p_ledc_config_t cfg = E32P_LEDC_CONFIG_DEFAULT();
    cfg.pin = TEST_PIN;
    e32p_ledc_handle_t h = NULL;
    TEST_ESP_OK(e32p_ledc_init(&cfg, &h));
    TEST_ASSERT_NOT_NULL(h);

    e32p_ledc_status_t st;
    TEST_ESP_OK(e32p_ledc_get_status(h, &st));
    TEST_ASSERT_FALSE(st.running);                 /* idle after init */
    TEST_ASSERT_EQUAL_UINT32((1u << 13) - 1u, st.max_duty);

    TEST_ESP_OK(e32p_ledc_start(h));
    TEST_ESP_OK(e32p_ledc_get_status(h, &st));
    TEST_ASSERT_TRUE(st.running);

    TEST_ESP_OK(e32p_ledc_stop(h));
    TEST_ESP_OK(e32p_ledc_get_status(h, &st));
    TEST_ASSERT_FALSE(st.running);

    TEST_ESP_OK(e32p_ledc_deinit(h));
}

TEST_CASE("e32p_ledc: duty percent and clamping", "[e32p_ledc]")
{
    e32p_ledc_config_t cfg = E32P_LEDC_CONFIG_DEFAULT();
    cfg.pin = TEST_PIN;
    e32p_ledc_handle_t h = NULL;
    TEST_ESP_OK(e32p_ledc_init(&cfg, &h));

    e32p_ledc_status_t st;
    TEST_ESP_OK(e32p_ledc_set_duty_percent(h, 100));
    TEST_ESP_OK(e32p_ledc_get_status(h, &st));
    TEST_ASSERT_EQUAL_UINT32(st.max_duty, st.duty);

    TEST_ESP_OK(e32p_ledc_set_duty(h, 0xFFFFFFFFu));   /* far above max */
    TEST_ESP_OK(e32p_ledc_get_status(h, &st));
    TEST_ASSERT_EQUAL_UINT32(st.max_duty, st.duty);    /* clamped */

    TEST_ESP_OK(e32p_ledc_deinit(h));
}

TEST_CASE("e32p_ledc: rejects NULL and invalid pin", "[e32p_ledc]")
{
    e32p_ledc_handle_t h = NULL;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_ledc_init(NULL, &h));

    e32p_ledc_config_t cfg = E32P_LEDC_CONFIG_DEFAULT();
    cfg.pin = 34;   /* input-only on ESP32, not output-capable */
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_ledc_init(&cfg, &h));
}
