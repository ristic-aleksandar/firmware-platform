/**
 * @file main.c
 * @brief 03_sysinfo_wdt — dumps system info + reset reason, then runs a
 *        watchdog-fed heartbeat.
 *
 * Proves the rest of the M2 spine: e32p_sysinfo (chip/app/reset/heap) and
 * e32p_wdt (task watchdog). The reset-reason line changes between a power
 * cycle, an EN reset, and a watchdog reboot.
 *
 * To see a task-watchdog reset for yourself: set FEED_THE_DOG to 0, reflash,
 * and after ~3 s the board panics and reboots; the next boot then reports
 * "reset: task watchdog".
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "e32p_sysinfo.h"
#include "e32p_wdt.h"

#define WDT_TIMEOUT_MS   3000
#define FEED_THE_DOG     1        /* set to 0 to trigger a task-watchdog reset */

static const char *TAG = "sysinfo_wdt";

void app_main(void)
{
    e32p_sysinfo_log();
    ESP_LOGI(TAG, "reset reason: %s", e32p_sysinfo_reset_reason_str());

    ESP_ERROR_CHECK(e32p_wdt_init(WDT_TIMEOUT_MS, true));
    ESP_ERROR_CHECK(e32p_wdt_watch_current_task());

    for (int i = 0; ; i++) {
#if FEED_THE_DOG
        ESP_ERROR_CHECK(e32p_wdt_feed());
#endif
        ESP_LOGI(TAG, "alive #%d  uptime=%llums  free_heap=%u", i,
                 (unsigned long long)e32p_sysinfo_uptime_ms(),
                 (unsigned)e32p_sysinfo_free_heap());
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
