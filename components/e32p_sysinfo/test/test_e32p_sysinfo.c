/**
 * @file test_e32p_sysinfo.c
 * @brief Unity tests for e32p_sysinfo. Run on-target.
 */
#include "unity.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "e32p_sysinfo.h"

TEST_CASE("e32p_sysinfo: get fills a plausible snapshot", "[e32p_sysinfo]")
{
    e32p_sysinfo_t info;
    TEST_ESP_OK(e32p_sysinfo_get(&info));
    TEST_ASSERT_GREATER_THAN_INT(0, info.cores);
    TEST_ASSERT_TRUE(info.chip_model[0] != '\0');
    TEST_ASSERT_TRUE(info.idf_version[0] != '\0');
}

TEST_CASE("e32p_sysinfo: get rejects NULL", "[e32p_sysinfo]")
{
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_sysinfo_get(NULL));
}

TEST_CASE("e32p_sysinfo: reset reason string is non-empty", "[e32p_sysinfo]")
{
    const char *s = e32p_sysinfo_reset_reason_str();
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_TRUE(s[0] != '\0');
}

TEST_CASE("e32p_sysinfo: uptime advances", "[e32p_sysinfo]")
{
    uint64_t t0 = e32p_sysinfo_uptime_ms();
    vTaskDelay(pdMS_TO_TICKS(20));
    uint64_t t1 = e32p_sysinfo_uptime_ms();
    /* Avoid Unity's 64-bit typed asserts (CONFIG_UNITY_ENABLE_64BIT is off by
       default in ESP-IDF) — a plain boolean comparison is portable. */
    TEST_ASSERT_TRUE(t1 > t0);
    TEST_ASSERT_TRUE(e32p_sysinfo_free_heap() > 0);
}
