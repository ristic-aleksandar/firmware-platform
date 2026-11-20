#pragma once
/**
 * @file e32p_event.h
 * @brief Thin facade over esp_event's default loop. Singleton / idempotent init.
 */
#include "esp_event.h"        /* esp_event_base_t, esp_event_handler_t, TickType_t */
#include "e32p_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create the default event loop. Idempotent — safe if already created
 *        (e.g. by Wi-Fi), in which case the existing loop is adopted.
 * @return ESP_OK or a driver error.
 */
esp_err_t e32p_event_init(void);

/**
 * @brief Delete the default event loop.
 * @return ESP_OK (no-op if not running) or a driver error.
 */
esp_err_t e32p_event_deinit(void);

/** @brief Whether the default loop is currently up. */
bool e32p_event_is_running(void);

/**
 * @brief Register a handler on the default loop.
 * @param[in] base     Event base (e.g. WIFI_EVENT, or your ESP_EVENT_DEFINE_BASE).
 * @param[in] id       Event id, or ESP_EVENT_ANY_ID.
 * @param[in] handler  Callback (must not be NULL); runs on the event task.
 * @param[in] arg      Opaque argument passed to @p handler.
 * @return ESP_OK, ESP_ERR_INVALID_ARG, or a driver error.
 */
esp_err_t e32p_event_handler_register(esp_event_base_t base, int32_t id,
                                      esp_event_handler_t handler, void *arg);

/**
 * @brief Unregister a handler from the default loop.
 * @param[in] base     Event base.
 * @param[in] id       Event id.
 * @param[in] handler  The handler previously registered (must not be NULL).
 * @return ESP_OK, ESP_ERR_INVALID_ARG, or a driver error.
 */
esp_err_t e32p_event_handler_unregister(esp_event_base_t base, int32_t id,
                                        esp_event_handler_t handler);

/**
 * @brief Post an event to the default loop.
 * @param[in] base            Event base.
 * @param[in] id              Event id.
 * @param[in] data            Optional payload copied into the loop (may be NULL).
 * @param[in] data_size       Size of @p data in bytes.
 * @param[in] ticks_to_wait   Max ticks to wait for queue space.
 * @return ESP_OK, ESP_ERR_TIMEOUT, or a driver error.
 */
esp_err_t e32p_event_post(esp_event_base_t base, int32_t id,
                          const void *data, size_t data_size, TickType_t ticks_to_wait);

#ifdef __cplusplus
}
#endif
