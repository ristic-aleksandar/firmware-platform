/**
 * @file test_e32p_pcnt.c
 * @brief Unity tests for e32p_pcnt. Uses io_loop_back so gpio_set_level generates
 *        countable edges with no wiring. Run on-target.
 */
#include "unity.h"
#include "e32p_pcnt.h"
#include "e32p_gpio.h"

#define T_GPIO   18

TEST_CASE("e32p_pcnt: counts internally-generated pulses", "[e32p_pcnt]")
{
    e32p_pcnt_config_t cfg = { .edge_gpio = T_GPIO, .high_limit = 1000, .low_limit = -1000,
                               .count_rising = true, .count_falling = false,
                               .io_loop_back = true };
    e32p_pcnt_handle_t p = NULL;
    TEST_ESP_OK(e32p_pcnt_init(&cfg, &p));
    TEST_ESP_OK(e32p_pcnt_clear(p));
    TEST_ESP_OK(e32p_pcnt_start(p));

    const int n = 25;
    for (int i = 0; i < n; i++) {
        e32p_gpio_set_level(T_GPIO, 1);
        e32p_gpio_set_level(T_GPIO, 0);
    }

    int count = -1;
    TEST_ESP_OK(e32p_pcnt_get_count(p, &count));
    TEST_ASSERT_EQUAL_INT(n, count);

    TEST_ESP_OK(e32p_pcnt_stop(p));
    TEST_ESP_OK(e32p_pcnt_deinit(p));
}

TEST_CASE("e32p_pcnt: rejects bad args", "[e32p_pcnt]")
{
    e32p_pcnt_handle_t p = NULL;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_pcnt_init(NULL, &p));

    /* no edge action selected */
    e32p_pcnt_config_t cfg = { .edge_gpio = T_GPIO, .high_limit = 100, .low_limit = -100,
                               .count_rising = false, .count_falling = false,
                               .io_loop_back = true };
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_pcnt_init(&cfg, &p));
}
