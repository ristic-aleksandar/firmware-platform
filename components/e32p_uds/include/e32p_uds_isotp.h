/**
 * @file e32p_uds_isotp.h
 * @brief Puts an e32p_uds server on the CAN bus through the ISO-TP link.
 *
 * The glue owns the server and runs it in a task of its own. Requests the ISO-TP
 * pump reassembles are copied into a small queue; the task hands them to the server,
 * calls e32p_uds_poll() every few milliseconds, and sends answers with
 * e32p_isotp_twai_send_sync(). Because that send returns only once the transfer is
 * done, an answer has left the transport before the next step runs — the reset
 * callback included. **Every server callback runs in this task.** Work that finishes
 * later reports back with e32p_uds_isotp_complete(), from any task.
 *
 * Addresses: physical requests on 0x7E1, answers on 0x7E9 (ECU 0x11), broadcasts on
 * 0x7DF — or the 29-bit normal-fixed equivalents when the ISO-TP Kconfig selects
 * them. The ISO-TP link allows one channel per receive ID, so on one link only one
 * server can own the broadcast address; see @ref e32p_uds_isotp_config_t::no_functional.
 *
 * Lifecycle: e32p_twai_init/start -> e32p_isotp_twai_start -> e32p_uds_isotp_start;
 * tear down in reverse with e32p_uds_isotp_stop first.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "e32p_uds.h"

#ifdef __cplusplus
extern "C" {
#endif

#define E32P_UDS_ECU_ADDR_DEFAULT    0x11u   /**< 0x7E1 -> 0x7E9                         */
#define E32P_UDS_TESTER_ADDR_DEFAULT 0xF1u
#define E32P_UDS_ISOTP_COMPLETE_MAX  64u     /**< most data e32p_uds_isotp_complete() carries */

/** @brief Glue configuration. Zeroed fields take the documented defaults. */
typedef struct {
    const e32p_uds_config_t *server;       /**< required; its `send` is replaced by the glue's   */
    uint8_t                  ecu_addr;     /**< 0 -> 0x11. 11-bit IDs need 0x10..0x17            */
    uint8_t                  tester_addr;  /**< 0 -> 0xF1                                        */
    bool                     no_functional;/**< true: leave the broadcast address to another
                                                server on the same link (e.g. OBD-II)           */
    uint16_t                 poll_ms;      /**< 0 -> 5                                           */
    uint32_t                 task_stack;   /**< 0 -> 4096                                        */
    int                      task_prio;    /**< 0 -> 5                                           */
    /** Gap this ECU asks a tester to leave between the frames of a long request, in ms
        (ISO-TP STmin, 0..127). 0, the default, means none: measured on a 1 Mbit bus, a
        reflash ran 8x faster than at 1 ms. Raise it only for a receiver that drops frames. */
    uint8_t                  rx_stmin_ms;
} e32p_uds_isotp_config_t;

/**
 * @brief Create the server, its ISO-TP channels and its task.
 * @return ESP_OK; ESP_ERR_INVALID_ARG; ESP_ERR_INVALID_STATE (already running, the
 *         ISO-TP link is not started, or another channel owns one of the IDs);
 *         ESP_ERR_NO_MEM
 */
esp_err_t e32p_uds_isotp_start(const e32p_uds_isotp_config_t *cfg);

/** @brief Stop the task, destroy the channels and the server. Idempotent. */
esp_err_t e32p_uds_isotp_stop(void);

/** @brief True between a successful start and stop. */
bool e32p_uds_isotp_running(void);

/**
 * @brief e32p_uds_complete(), safe to call from any task: the outcome is queued for
 *        the glue task, which delivers it within one poll period.
 * @param[in] nrc   E32P_UDS_NRC_OK or the negative response
 * @param[in] data  response bytes after the echo, at most E32P_UDS_ISOTP_COMPLETE_MAX
 * @param[in] len   length of @p data
 * @return ESP_OK; ESP_ERR_INVALID_ARG; ESP_ERR_INVALID_STATE if not running;
 *         ESP_ERR_TIMEOUT if the queue stayed full
 */
esp_err_t e32p_uds_isotp_complete(e32p_uds_nrc_t nrc, const uint8_t *data, uint16_t len);

/** @brief Requests dropped because the request queue was full (diagnostics). */
uint32_t e32p_uds_isotp_dropped(void);

#ifdef __cplusplus
}
#endif
