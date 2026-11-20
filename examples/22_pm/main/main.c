/**
 * @file main.c
 * @brief 22_pm — low-power demo. Shows all three pieces of a sleep duty cycle:
 *   1. an RTC-retained boot counter that survives deep sleep,
 *   2. the reported wake cause each boot,
 *   3. a light sleep (returns) followed by a deep sleep (resets + repeats).
 *
 * Watch the serial log: every ~7 s the board wakes, increments the counter,
 * light-sleeps 2 s, then deep-sleeps 5 s. `boot_count` keeps climbing because
 * RTC slow memory is retained across deep sleep.
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "e32p_pm.h"

static const char *TAG = "pm_demo";

/* Retained across deep sleep (RTC slow memory); zero-initialised on cold boot. */
RTC_DATA_ATTR static int boot_count;

void app_main(void)
{
    boot_count++;
    ESP_LOGI(TAG, "boot #%d — woke by: %s", boot_count,
             e32p_pm_wake_cause_str(e32p_pm_wake_cause()));

    /* Light sleep: returns here after the timer fires. */
    ESP_LOGI(TAG, "light-sleeping 2 s (timer)...");
    e32p_pm_disable_all_wakeup();
    e32p_pm_enable_timer_wakeup(2 * 1000 * 1000);
    e32p_pm_light_sleep();
    ESP_LOGI(TAG, "back from light sleep — woke by: %s",
             e32p_pm_wake_cause_str(e32p_pm_wake_cause()));

    /* Deep sleep: resets the chip; app_main runs again on wake. */
    ESP_LOGI(TAG, "deep-sleeping 5 s (chip will reset and boot_count will rise)...");
    vTaskDelay(pdMS_TO_TICKS(100));   /* let the UART drain before we power down */
    e32p_pm_deep_sleep_for(5 * 1000 * 1000);
}
