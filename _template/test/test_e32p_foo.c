/**
 * @file test_e32p_foo.c
 * @brief Unity test template: init -> status -> start/stop -> deinit, plus an
 *        invalid-arg error path. Run on-target (source of truth per plan).
 */
#include "unity.h"
#include "e32p_foo.h"

TEST_CASE("e32p_foo: init/deinit lifecycle", "[e32p_foo]")
{
    e32p_foo_config_t cfg = E32P_FOO_CONFIG_DEFAULT();
    e32p_foo_handle_t h = NULL;

    TEST_ESP_OK(e32p_foo_init(&cfg, &h));
    TEST_ASSERT_NOT_NULL(h);

    e32p_foo_status_t st;
    TEST_ESP_OK(e32p_foo_get_status(h, &st));
    TEST_ASSERT_FALSE(st.running);

    TEST_ESP_OK(e32p_foo_start(h));
    TEST_ESP_OK(e32p_foo_get_status(h, &st));
    TEST_ASSERT_TRUE(st.running);

    TEST_ESP_OK(e32p_foo_stop(h));
    TEST_ESP_OK(e32p_foo_deinit(h));
}

TEST_CASE("e32p_foo: init rejects NULL args", "[e32p_foo]")
{
    e32p_foo_handle_t h = NULL;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_foo_init(NULL, &h));
}
