/**
 * @file test_e32p_pm.c
 * @brief Unity tests for e32p_pm. The wake-cause mapping and validation are pure
 *        and fully covered; light sleep is exercised for real on-target (arm a
 *        short timer, sleep, confirm the wake cause). Deep sleep resets the chip,
 *        so it is demonstrated in the example rather than asserted here.
 */
#include "unity.h"
#include "e32p_pm.h"

TEST_CASE("e32p_pm: wake-cause maps from esp constants", "[e32p_pm]")
{
    TEST_ASSERT_EQUAL(E32P_PM_WAKE_TIMER, e32p_pm_wake_cause_from_esp(ESP_SLEEP_WAKEUP_TIMER));
    TEST_ASSERT_EQUAL(E32P_PM_WAKE_EXT0,  e32p_pm_wake_cause_from_esp(ESP_SLEEP_WAKEUP_EXT0));
    TEST_ASSERT_EQUAL(E32P_PM_WAKE_EXT1,  e32p_pm_wake_cause_from_esp(ESP_SLEEP_WAKEUP_EXT1));
    TEST_ASSERT_EQUAL(E32P_PM_WAKE_TOUCH, e32p_pm_wake_cause_from_esp(ESP_SLEEP_WAKEUP_TOUCHPAD));
    TEST_ASSERT_EQUAL(E32P_PM_WAKE_ULP,   e32p_pm_wake_cause_from_esp(ESP_SLEEP_WAKEUP_ULP));
    TEST_ASSERT_EQUAL(E32P_PM_WAKE_UNKNOWN, e32p_pm_wake_cause_from_esp(ESP_SLEEP_WAKEUP_UNDEFINED));
}

TEST_CASE("e32p_pm: wake-cause strings are stable and non-empty", "[e32p_pm]")
{
    TEST_ASSERT_EQUAL_STRING("timer",   e32p_pm_wake_cause_str(E32P_PM_WAKE_TIMER));
    TEST_ASSERT_EQUAL_STRING("ext1",    e32p_pm_wake_cause_str(E32P_PM_WAKE_EXT1));
    TEST_ASSERT_EQUAL_STRING("unknown", e32p_pm_wake_cause_str(E32P_PM_WAKE_UNKNOWN));
    TEST_ASSERT_EQUAL_STRING("unknown", e32p_pm_wake_cause_str((e32p_pm_wake_cause_t)999));
}

TEST_CASE("e32p_pm: RTC-capable pins are recognised", "[e32p_pm]")
{
    TEST_ASSERT_TRUE(e32p_pm_is_rtc_gpio(2));    /* RTC */
    TEST_ASSERT_TRUE(e32p_pm_is_rtc_gpio(25));   /* RTC */
    TEST_ASSERT_TRUE(e32p_pm_is_rtc_gpio(33));   /* RTC */
    TEST_ASSERT_FALSE(e32p_pm_is_rtc_gpio(21));  /* not RTC */
    TEST_ASSERT_FALSE(e32p_pm_is_rtc_gpio(23));  /* not RTC */
    TEST_ASSERT_FALSE(e32p_pm_is_rtc_gpio(-1));  /* invalid */
}

TEST_CASE("e32p_pm: wake-source config rejects bad args", "[e32p_pm]")
{
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_pm_enable_timer_wakeup(0));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_pm_enable_ext0_wakeup(21, 1));  /* non-RTC pin */
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_pm_enable_ext0_wakeup(2, 2));   /* bad level */
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_pm_enable_ext1_wakeup(0, E32P_PM_EXT1_ANY_HIGH));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_pm_enable_gpio_wakeup(2, 3));   /* bad level */
    e32p_pm_disable_all_wakeup();
}

TEST_CASE("e32p_pm: light sleep wakes on the timer", "[e32p_pm]")
{
    TEST_ESP_OK(e32p_pm_disable_all_wakeup());
    TEST_ESP_OK(e32p_pm_enable_timer_wakeup(20000));   /* 20 ms */
    TEST_ESP_OK(e32p_pm_light_sleep());                /* blocks, then returns */
    TEST_ASSERT_EQUAL(E32P_PM_WAKE_TIMER, e32p_pm_wake_cause());
    e32p_pm_disable_all_wakeup();
}
