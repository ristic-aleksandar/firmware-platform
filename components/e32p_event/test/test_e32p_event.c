/**
 * @file test_e32p_event.c
 * @brief Unity tests for e32p_event: idempotent init, dispatch round-trip,
 *        and argument validation. Run on-target.
 */
#include "unity.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "e32p_event.h"

ESP_EVENT_DEFINE_BASE(TEST_EVENTS);
enum { TEST_EVENT_PING };

static volatile bool s_got_event;

static void on_test_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)base; (void)id; (void)data;
    s_got_event = true;
}

TEST_CASE("e32p_event: init is idempotent and reports running", "[e32p_event]")
{
    TEST_ESP_OK(e32p_event_init());
    TEST_ESP_OK(e32p_event_init());          /* second call is a no-op */
    TEST_ASSERT_TRUE(e32p_event_is_running());
}

TEST_CASE("e32p_event: register / post / dispatch", "[e32p_event]")
{
    TEST_ESP_OK(e32p_event_init());
    s_got_event = false;
    TEST_ESP_OK(e32p_event_handler_register(TEST_EVENTS, TEST_EVENT_PING, on_test_event, NULL));
    TEST_ESP_OK(e32p_event_post(TEST_EVENTS, TEST_EVENT_PING, NULL, 0, portMAX_DELAY));

    /* Let the event task dispatch. */
    vTaskDelay(pdMS_TO_TICKS(100));
    TEST_ASSERT_TRUE(s_got_event);

    TEST_ESP_OK(e32p_event_handler_unregister(TEST_EVENTS, TEST_EVENT_PING, on_test_event));
}

TEST_CASE("e32p_event: NULL handler rejected", "[e32p_event]")
{
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      e32p_event_handler_register(TEST_EVENTS, TEST_EVENT_PING, NULL, NULL));
}
