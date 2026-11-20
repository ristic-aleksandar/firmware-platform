/**
 * @file test_e32p_touch.c
 * @brief Unity tests for e32p_touch. Run on-target (no external parts; an
 *        untouched pad still reads a positive count).
 */
#include "unity.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "e32p_touch.h"

TEST_CASE("e32p_touch: init / config / read", "[e32p_touch]")
{
    TEST_ESP_OK(e32p_touch_init());
    TEST_ESP_OK(e32p_touch_init());          /* idempotent */
    TEST_ESP_OK(e32p_touch_config_pad(0));   /* T0 = GPIO4 */

    vTaskDelay(pdMS_TO_TICKS(50));           /* let the FSM take a measurement */
    uint32_t v = 0;
    TEST_ESP_OK(e32p_touch_read(0, &v));
    TEST_ASSERT_TRUE(v > 0);                 /* untouched pad reads a positive count */
}

TEST_CASE("e32p_touch: rejects bad args", "[e32p_touch]")
{
    TEST_ESP_OK(e32p_touch_init());
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_touch_config_pad(99));

    uint32_t v = 0;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_touch_read(0, NULL));
    (void)v;
}
