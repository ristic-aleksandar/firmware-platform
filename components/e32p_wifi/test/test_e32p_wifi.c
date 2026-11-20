/**
 * @file test_e32p_wifi.c
 * @brief Unity tests for e32p_wifi. Argument validation only — these do NOT bring
 *        up the Wi-Fi stack (arg checks run before init), so they are safe to run
 *        headless in the shared runner. Real connect/AP tests need an AP (I-slot).
 */
#include "unity.h"
#include "e32p_wifi.h"

TEST_CASE("e32p_wifi: rejects bad args (no stack init)", "[e32p_wifi]")
{
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_wifi_sta_connect(NULL, "pw", 1000));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_wifi_ap_start(NULL, "pw", 1, 4));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_wifi_ap_start("ap", "pw", 99, 4));  /* bad channel */
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_wifi_ap_start("ap", "pw", 1, 99));  /* bad max_conn */

    char ip[8];
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_wifi_get_ip(ip, sizeof(ip)));       /* buffer < 16 */
    TEST_ASSERT_FALSE(e32p_wifi_is_connected());
}
