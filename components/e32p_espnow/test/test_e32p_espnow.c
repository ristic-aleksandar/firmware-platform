/**
 * @file test_e32p_espnow.c
 * @brief Unity tests for e32p_espnow. Argument validation only — these run
 *        before init, so they do NOT bring up the radio (safe in the shared
 *        runner). A real send/receive test needs two boards (I-slot).
 */
#include "unity.h"
#include "e32p_espnow.h"

TEST_CASE("e32p_espnow: rejects bad args (no radio init)", "[e32p_espnow]")
{
    uint8_t mac[6] = { 1, 2, 3, 4, 5, 6 };
    uint8_t buf[4] = { 0 };

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_espnow_send(NULL, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_espnow_send(mac, NULL, sizeof(buf)));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_espnow_send(mac, buf, 0));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_espnow_send(mac, buf, 300));   /* > 250 */
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_espnow_broadcast(NULL, sizeof(buf)));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_espnow_get_mac(NULL));
}
