#pragma once
/**
 * @file e32p_wdt.h
 * @brief Thin facade over the Task Watchdog Timer (TWDT). Singleton, idempotent.
 */
#include <stdint.h>
#include <stdbool.h>
#include "e32p_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Configure the Task WDT. Idempotent — if the system already started it
 *        (CONFIG_ESP_TASK_WDT_INIT), this reconfigures it instead.
 * @param[in] timeout_ms         Watchdog period in milliseconds.
 * @param[in] panic_on_timeout   true = panic + reboot on timeout; false = warn only.
 * @return ESP_OK or a driver error.
 */
esp_err_t e32p_wdt_init(uint32_t timeout_ms, bool panic_on_timeout);

/**
 * @brief Deinitialize the Task WDT.
 * @return ESP_OK (no-op if not initialized). Fails if tasks are still watched —
 *         unwatch them first.
 */
esp_err_t e32p_wdt_deinit(void);

/** @brief Subscribe the calling task to the WDT. @return ESP_OK / driver error. */
esp_err_t e32p_wdt_watch_current_task(void);
/** @brief Unsubscribe the calling task. @return ESP_OK / driver error. */
esp_err_t e32p_wdt_unwatch_current_task(void);
/** @brief Feed the WDT for the calling task (call before the timeout). */
esp_err_t e32p_wdt_feed(void);

#ifdef __cplusplus
}
#endif
