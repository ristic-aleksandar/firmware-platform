/**
 * @file e32p_wdt.c
 * @brief e32p_wdt implementation — Task Watchdog facade.
 */
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "e32p_wdt.h"

static const char *TAG = "e32p_wdt";

esp_err_t e32p_wdt_init(uint32_t timeout_ms, bool panic_on_timeout)
{
    const esp_task_wdt_config_t cfg = {
        .timeout_ms     = timeout_ms,
        .idle_core_mask = 0,               /* watch only the tasks we subscribe */
        .trigger_panic  = panic_on_timeout,
    };
    /* Choose init vs. reconfigure by state, rather than calling init and
     * catching the error: esp_task_wdt_init() logs its own ERROR when the TWDT
     * is already up (the system starts it via CONFIG_ESP_TASK_WDT_INIT).
     * esp_task_wdt_status(NULL) returns INVALID_STATE only when it is down. */
    esp_err_t err;
    if (esp_task_wdt_status(NULL) == ESP_ERR_INVALID_STATE) {
        err = esp_task_wdt_init(&cfg);
    } else {
        err = esp_task_wdt_reconfigure(&cfg);
    }
    E32P_RETURN_ON_ERR(err, TAG);
    ESP_LOGI(TAG, "task WDT set: timeout=%ums panic=%d", (unsigned)timeout_ms, panic_on_timeout);
    return ESP_OK;
}

esp_err_t e32p_wdt_deinit(void)
{
    esp_err_t err = esp_task_wdt_deinit();
    if (err == ESP_ERR_INVALID_STATE) {
        return ESP_OK;   /* not initialized */
    }
    E32P_RETURN_ON_ERR(err, TAG);
    return ESP_OK;
}

esp_err_t e32p_wdt_watch_current_task(void)
{
    E32P_RETURN_ON_ERR(esp_task_wdt_add(NULL), TAG);   /* NULL = calling task */
    return ESP_OK;
}

esp_err_t e32p_wdt_unwatch_current_task(void)
{
    E32P_RETURN_ON_ERR(esp_task_wdt_delete(NULL), TAG);
    return ESP_OK;
}

esp_err_t e32p_wdt_feed(void)
{
    return esp_task_wdt_reset();
}
