/**
 * @file test_e32p_nvs.c
 * @brief Unity tests for e32p_nvs: init idempotency, handle + one-shot round
 *        trips, missing-key behavior, and argument validation. Run on-target.
 */
#include "unity.h"
#include "e32p_nvs.h"

#define TEST_NS   "e32p_test"

TEST_CASE("e32p_nvs: init is idempotent", "[e32p_nvs]")
{
    TEST_ESP_OK(e32p_nvs_init());
    TEST_ESP_OK(e32p_nvs_init());
}

TEST_CASE("e32p_nvs: handle i32 round-trip", "[e32p_nvs]")
{
    TEST_ESP_OK(e32p_nvs_init());

    e32p_nvs_handle_t h = NULL;
    TEST_ESP_OK(e32p_nvs_open(TEST_NS, E32P_NVS_READWRITE, &h));
    TEST_ESP_OK(e32p_nvs_set_i32(h, "answer", 42));
    TEST_ESP_OK(e32p_nvs_commit(h));
    TEST_ESP_OK(e32p_nvs_close(h));

    int32_t v = 0;
    TEST_ESP_OK(e32p_nvs_read_i32(TEST_NS, "answer", &v));
    TEST_ASSERT_EQUAL_INT32(42, v);
}

TEST_CASE("e32p_nvs: one-shot write/read and missing key", "[e32p_nvs]")
{
    TEST_ESP_OK(e32p_nvs_init());

    TEST_ESP_OK(e32p_nvs_write_i32(TEST_NS, "count", 7));
    int32_t v = 0;
    TEST_ESP_OK(e32p_nvs_read_i32(TEST_NS, "count", &v));
    TEST_ASSERT_EQUAL_INT32(7, v);

    /* absent key -> reported via the not-found predicate */
    TEST_ASSERT_TRUE(e32p_nvs_is_not_found(e32p_nvs_read_i32(TEST_NS, "nope", &v)));
}

TEST_CASE("e32p_nvs: argument validation", "[e32p_nvs]")
{
    TEST_ESP_OK(e32p_nvs_init());

    e32p_nvs_handle_t h = NULL;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_nvs_open(NULL, E32P_NVS_READONLY, &h));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_nvs_commit(NULL));
}
