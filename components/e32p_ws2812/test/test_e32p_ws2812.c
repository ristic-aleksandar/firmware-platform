/**
 * @file test_e32p_ws2812.c
 * @brief Unity tests for e32p_ws2812. Headless-safe: the RMT engine clocks the
 *        frame out through DMA whether or not a strip is connected, so
 *        init/set/refresh/deinit all exercise the real driver path.
 */
#include "unity.h"
#include "e32p_ws2812.h"

TEST_CASE("e32p_ws2812: init / set / refresh / deinit", "[e32p_ws2812]")
{
    e32p_ws2812_handle_t h = NULL;
    TEST_ESP_OK(e32p_ws2812_init(18, 8, &h));
    TEST_ASSERT_NOT_NULL(h);

    TEST_ESP_OK(e32p_ws2812_clear(h));
    TEST_ESP_OK(e32p_ws2812_set_pixel(h, 0, 255, 0, 0));
    TEST_ESP_OK(e32p_ws2812_set_pixel(h, 7, 0, 0, 255));
    TEST_ESP_OK(e32p_ws2812_refresh(h));

    TEST_ESP_OK(e32p_ws2812_deinit(h));
}

TEST_CASE("e32p_ws2812: rejects bad args", "[e32p_ws2812]")
{
    e32p_ws2812_handle_t h = NULL;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_ws2812_init(18, 0, &h));   /* 0 leds */
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_ws2812_init(-1, 8, &h));   /* bad gpio */

    TEST_ESP_OK(e32p_ws2812_init(18, 4, &h));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_ws2812_set_pixel(h, 4, 1, 2, 3)); /* out of range */
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_ws2812_set_pixel(NULL, 0, 1, 2, 3));
    TEST_ESP_OK(e32p_ws2812_deinit(h));
}
