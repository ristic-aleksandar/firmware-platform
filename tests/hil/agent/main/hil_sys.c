/**
 * @file hil_sys.c
 * @brief Foundation HIL tests — event, nvs, wdt (single board, no wiring).
 */
#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "e32p_event.h"
#include "e32p_nvs.h"
#include "e32p_wdt.h"
#include "e32p_time.h"
#include "hil_agent.h"
#include "hil_sys.h"

ESP_EVENT_DEFINE_BASE(HIL_TEST_EVENT);

static volatile bool s_evt_fired;
static void on_test_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)base; (void)id; (void)data;
    s_evt_fired = true;
}

hil_verdict_t hil_sys_event(char *d, size_t n)
{
    s_evt_fired = false;
    if (e32p_event_init() != ESP_OK) {
        snprintf(d, n, "event init failed");
        return HIL_FAIL;
    }
    if (e32p_event_handler_register(HIL_TEST_EVENT, 0, on_test_event, NULL) != ESP_OK) {
        snprintf(d, n, "handler register failed");
        return HIL_FAIL;
    }
    e32p_event_post(HIL_TEST_EVENT, 0, NULL, 0, pdMS_TO_TICKS(100));
    E32P_ASSERT_BLOCKING_MS(5);       /* 20 x 5 ms = a real 100 ms dispatch budget (D-01) */
    for (int i = 0; i < 20 && !s_evt_fired; i++) {
        vTaskDelay(pdMS_TO_TICKS(5));  /* dispatch runs on the event task */
    }
    e32p_event_handler_unregister(HIL_TEST_EVENT, 0, on_test_event);
    if (!s_evt_fired) {
        snprintf(d, n, "handler not dispatched");
        return HIL_FAIL;
    }
    snprintf(d, n, "register/post/dispatch ok");
    return HIL_PASS;
}

hil_verdict_t hil_sys_nvs(char *d, size_t n)
{
    if (e32p_nvs_init() != ESP_OK) {
        snprintf(d, n, "nvs init failed");
        return HIL_FAIL;
    }
    if (e32p_nvs_write_i32("hiltest", "k", 0x1234) != ESP_OK) {
        snprintf(d, n, "write failed");
        return HIL_FAIL;
    }
    int32_t v = 0;
    if (e32p_nvs_read_i32("hiltest", "k", &v) != ESP_OK || v != 0x1234) {
        snprintf(d, n, "read back %ld", (long)v);
        return HIL_FAIL;
    }
    snprintf(d, n, "i32 round-trip ok");
    return HIL_PASS;
}

hil_verdict_t hil_sys_wdt(char *d, size_t n)
{
    if (e32p_wdt_init(5000, false) != ESP_OK) {
        snprintf(d, n, "wdt init failed");
        return HIL_FAIL;
    }
    if (e32p_wdt_watch_current_task() != ESP_OK) {
        snprintf(d, n, "watch failed");
        return HIL_FAIL;
    }
    if (e32p_wdt_feed() != ESP_OK) {
        snprintf(d, n, "feed failed");
        return HIL_FAIL;
    }
    if (e32p_wdt_unwatch_current_task() != ESP_OK) {
        snprintf(d, n, "unwatch failed");
        return HIL_FAIL;
    }
    snprintf(d, n, "init/watch/feed/unwatch ok");
    return HIL_PASS;
}
