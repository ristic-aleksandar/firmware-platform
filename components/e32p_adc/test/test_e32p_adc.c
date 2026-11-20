/**
 * @file test_e32p_adc.c
 * @brief Unity tests for e32p_adc. Reads ADC1 headless (a floating pin still
 *        yields an in-range count). Run on-target.
 */
#include "unity.h"
#include "e32p_adc.h"

#define T_UNIT   1
#define T_CHAN   6   /* ADC1_CH6 = GPIO34 */

TEST_CASE("e32p_adc: init / config / raw read in range", "[e32p_adc]")
{
    e32p_adc_handle_t adc = NULL;
    TEST_ESP_OK(e32p_adc_init(T_UNIT, &adc));
    TEST_ESP_OK(e32p_adc_config_channel(adc, T_CHAN, E32P_ADC_ATTEN_12DB));

    int raw = -1;
    TEST_ESP_OK(e32p_adc_read_raw(adc, T_CHAN, &raw));
    TEST_ASSERT_TRUE(raw >= 0 && raw <= 4095);

    TEST_ESP_OK(e32p_adc_deinit(adc));
}

TEST_CASE("e32p_adc: rejects bad args", "[e32p_adc]")
{
    e32p_adc_handle_t adc = NULL;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_adc_init(5, &adc));    /* bad unit */
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_adc_init(1, NULL));
}
