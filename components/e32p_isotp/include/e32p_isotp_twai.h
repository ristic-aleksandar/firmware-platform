#pragma once
/**
 * @file e32p_isotp_twai.h
 * @brief Binds e32p_isotp channels to the single TWAI controller: one pump
 *        task, ID-based routing to N channels, and blocking send/recv helpers.
 *
 * The ESP32 has one TWAI peripheral and `e32p_twai` is a singleton, so this
 * link is a singleton too. It does **not** install or start TWAI — the
 * application does that first (pins, bitrate, mode), exactly like `e32p_j1939`.
 *
 * ### Lifecycle (D-03)
 *
 *     e32p_twai_init() -> e32p_twai_start() -> e32p_isotp_twai_start()
 *       -> _channel() / _attach() -> work
 *       -> e32p_isotp_twai_stop() -> e32p_twai_stop() -> e32p_twai_deinit()
 *
 * @ref e32p_isotp_twai_stop **hard-joins** the pump before returning. That is
 * not a tidiness detail: `e32p_twai_deinit()` uninstalls the driver, which
 * deletes the RX queue, and a task still blocked in `xQueueReceive` on a deleted
 * queue is undefined behaviour. Frame-stealing by a lingering pump across
 * sequentially-run tests is the *benign* failure here; the crash is the real
 * one. **Calling `e32p_twai_deinit()` while a link is running is a programming
 * error** — `_stop()` is the only safe teardown path.
 *
 * ### Two ways to use a channel
 *
 * - `cfg->on_rx == NULL` — **sync mode.** The link installs its own receive
 *   callback and buffers completed payloads for @ref e32p_isotp_twai_recv_sync.
 * - `cfg->on_rx != NULL` — **server mode.** Your callback is invoked directly
 *   from the pump task, which is what an OBD-II or UDS server wants.
 */
#include "e32p_isotp.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Link configuration. Zeroed fields take the documented defaults. */
typedef struct {
    uint32_t block_ms;   /*!< Pump idle block. 0 -> portTICK_PERIOD_MS.        */
    uint32_t task_stack; /*!< 0 -> 4096.                                       */
    int      task_prio;  /*!< 0 -> 10.                                         */
    uint8_t  sync_depth; /*!< Buffered payloads per sync channel. 0 -> 1.      */
} e32p_isotp_twai_config_t;

/**
 * @brief Start the pump. `e32p_twai` must already be installed and started.
 * @param[in] cfg  Optional; NULL takes every default.
 * @return ESP_OK; ESP_ERR_INVALID_STATE if already started; ESP_ERR_NO_MEM.
 */
esp_err_t e32p_isotp_twai_start(const e32p_isotp_twai_config_t *cfg);

/**
 * @brief Stop the pump and detach every channel. Idempotent — returns ESP_OK
 *        when already stopped, matching `e32p_twai_deinit`.
 * @return ESP_OK, or ESP_ERR_TIMEOUT if the pump did not join. On a timeout the
 *         caller must **not** deinit TWAI: the task may still touch the queue.
 */
esp_err_t e32p_isotp_twai_stop(void);

/** @brief True while the pump task is running. */
bool e32p_isotp_twai_running(void);

/**
 * @brief Route frames matching @p h's receive ID to it.
 * @return ESP_OK; ESP_ERR_INVALID_STATE if the link is stopped or another
 *         channel already claims that ID; ESP_ERR_NO_MEM at the channel cap.
 */
esp_err_t e32p_isotp_twai_attach(e32p_isotp_handle_t h);

/** @brief Stop routing to @p h. Does not destroy the channel. */
esp_err_t e32p_isotp_twai_detach(e32p_isotp_handle_t h);

/**
 * @brief Create a channel whose transmit sink is `e32p_twai_transmit`, wire the
 *        blocking helpers, and attach it. Destroy with
 *        @ref e32p_isotp_twai_destroy_channel, not `e32p_isotp_deinit`.
 * @return ESP_OK, ESP_ERR_INVALID_ARG, ESP_ERR_INVALID_STATE, ESP_ERR_NO_MEM.
 */
esp_err_t e32p_isotp_twai_channel(const e32p_isotp_config_t *cfg,
                                  e32p_isotp_handle_t *out);

/** @brief Detach, free the link's per-channel state, and destroy the channel.
 *         Safe before or after e32p_isotp_twai_stop(). */
esp_err_t e32p_isotp_twai_destroy_channel(e32p_isotp_handle_t h);

/**
 * @brief Blocking send. Returns once the transfer completes or @p timeout_ms
 *        elapses.
 *
 * @warning **Never call this from an `on_rx` / `on_tx_done` callback.** Those
 *          run in the pump task, and waiting there for the pump to make
 *          progress self-deadlocks. Use @ref e32p_isotp_send instead — it only
 *          arms the session and returns. Violations are caught and rejected
 *          with ESP_ERR_INVALID_STATE rather than hanging.
 *
 * @return ESP_OK; ESP_ERR_TIMEOUT; ESP_ERR_NO_MEM (peer sent FC.OVFLW);
 *         ESP_ERR_INVALID_STATE (called from the pump task, or TX busy).
 */
esp_err_t e32p_isotp_twai_send_sync(e32p_isotp_handle_t h, const uint8_t *data,
                                    uint16_t len, uint32_t timeout_ms);

/**
 * @brief Blocking receive of one complete payload. Sync-mode channels only
 *        (those created with `on_rx == NULL`).
 * @return ESP_OK; ESP_ERR_TIMEOUT; ESP_ERR_INVALID_SIZE (payload exceeds
 *         @p cap); ESP_ERR_INVALID_STATE (channel is in server mode).
 */
esp_err_t e32p_isotp_twai_recv_sync(e32p_isotp_handle_t h, uint8_t *buf,
                                    uint16_t cap, uint16_t *out_len,
                                    uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
