/**
 * @file test_e32p_gptimer.c
 * @brief Unity tests for e32p_gptimer. Run on-target.
 */
#include "unity.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "e32p_gptimer.h"

static volatile uint32_t s_test_alarms;

static void test_cb(void *arg)
{
    (void)arg;
    s_test_alarms++;
}

TEST_CASE("e32p_gptimer: periodic alarm fires and count advances", "[e32p_gptimer]")
{
    s_test_alarms = 0;
    e32p_gptimer_config_t cfg = { .resolution_hz = 1000000,   /* 1 us/tick */
                                  .alarm_period_ticks = 10000, /* 10 ms */
                                  .auto_reload = true };
    e32p_gptimer_handle_t t = NULL;
    TEST_ESP_OK(e32p_gptimer_init(&cfg, test_cb, NULL, &t));

    uint64_t c0 = 0;
    TEST_ESP_OK(e32p_gptimer_get_count(t, &c0));
    TEST_ESP_OK(e32p_gptimer_start(t));
    vTaskDelay(pdMS_TO_TICKS(100));            /* ~10 alarms */
    TEST_ESP_OK(e32p_gptimer_stop(t));

    uint64_t c1 = 0;
    TEST_ESP_OK(e32p_gptimer_get_count(t, &c1));
    TEST_ASSERT_TRUE(s_test_alarms >= 5);      /* several fired in 100 ms */
    TEST_ASSERT_TRUE(c1 > c0);

    TEST_ESP_OK(e32p_gptimer_deinit(t));
}

TEST_CASE("e32p_gptimer: rejects bad args", "[e32p_gptimer]")
{
    e32p_gptimer_handle_t t = NULL;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_gptimer_init(NULL, NULL, NULL, &t));

    e32p_gptimer_config_t cfg = { .resolution_hz = 0, .alarm_period_ticks = 1000,
                                  .auto_reload = true };
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_gptimer_init(&cfg, NULL, NULL, &t));
}
