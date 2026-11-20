/**
 * @file main.c
 * @brief Unity test runner — auto-runs every registered e32p_* unit test and
 *        prints a pass/fail summary. No input needed.
 *
 * The suites come from the test/ directories of the components listed in
 * TEST_COMPONENTS (see the project CMakeLists). All run headless on a bare
 * board (UART loopback, I2C scan, SPI transfer, etc. need no external parts).
 */
#include "unity.h"
#include "unity_test_runner.h"
#include "esp_log.h"

static const char *TAG = "unit_runner";

void app_main(void)
{
    ESP_LOGI(TAG, "running all e32p unit tests...");
    /* unity_run_all_tests() only loops over the registered cases; the
       "N Tests N Failures N Ignored" summary comes from UNITY_END(). */
    UNITY_BEGIN();
    unity_run_all_tests();
    UNITY_END();
}
