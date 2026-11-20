/**
 * @file test_e32p_wdt.c
 * @brief Unity tests for e32p_wdt: configure, subscribe, feed, unsubscribe.
 *        Run on-target. Uses panic=false so a missed feed can't reboot the runner.
 */
#include "unity.h"
#include "e32p_wdt.h"

TEST_CASE("e32p_wdt: init / watch / feed / unwatch", "[e32p_wdt]")
{
    TEST_ESP_OK(e32p_wdt_init(5000, false));
    TEST_ESP_OK(e32p_wdt_watch_current_task());
    TEST_ESP_OK(e32p_wdt_feed());
    TEST_ESP_OK(e32p_wdt_unwatch_current_task());
    /* Deliberately no deinit: system idle tasks may remain subscribed. */
}

TEST_CASE("e32p_wdt: init is idempotent (reconfigure path)", "[e32p_wdt]")
{
    TEST_ESP_OK(e32p_wdt_init(4000, false));
    TEST_ESP_OK(e32p_wdt_init(6000, false));
}
