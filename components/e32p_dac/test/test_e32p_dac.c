/**
 * @file test_e32p_dac.c
 * @brief Unity tests for e32p_dac. Run on-target (no external parts).
 */
#include "unity.h"
#include "e32p_dac.h"

TEST_CASE("e32p_dac: init / set / deinit", "[e32p_dac]")
{
    e32p_dac_handle_t dac = NULL;
    TEST_ESP_OK(e32p_dac_init(0, &dac));       /* GPIO25 */
    TEST_ASSERT_NOT_NULL(dac);

    TEST_ESP_OK(e32p_dac_set_raw(dac, 128));
    TEST_ESP_OK(e32p_dac_set_voltage(dac, 1650));
    TEST_ESP_OK(e32p_dac_set_voltage(dac, 9999));   /* clamps to full-scale */

    TEST_ESP_OK(e32p_dac_deinit(dac));
}

TEST_CASE("e32p_dac: rejects bad args", "[e32p_dac]")
{
    e32p_dac_handle_t dac = NULL;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_dac_init(3, &dac));   /* bad channel */
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_dac_init(0, NULL));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_dac_set_raw(NULL, 100));
}
