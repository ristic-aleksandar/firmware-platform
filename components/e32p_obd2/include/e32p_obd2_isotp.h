/**
 * @file e32p_obd2_isotp.h
 * @brief Puts an OBD-II server on the bus: two ISO-TP channels on the standard
 *        OBD addresses, feeding one @ref e32p_obd2_handle_t.
 *
 * Bring `e32p_twai` up and start the ISO-TP link first; this only adds channels
 * to it. Addressing (11-bit or 29-bit normal-fixed) follows `e32p_isotp`'s
 * Kconfig choice, so a build talks one or the other, never both.
 *
 *     11-bit:  0x7DF functional, 0x7E0 physical  ->  answers on 0x7E8
 *     29-bit:  0x18DB33F1,       0x18DA10F1      ->  answers on 0x18DAF110
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "e32p_core.h"
#include "e32p_obd2.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Default ECU address: 0x10, which is 0x7E0 / 0x7E8 in 11-bit terms. */
#define E32P_OBD2_ECU_ADDR_DEFAULT    0x10u

/** @brief Default tester address, fixed by the standard. */
#define E32P_OBD2_TESTER_ADDR_DEFAULT 0xF1u

/** @brief Binding configuration. */
typedef struct {
    e32p_obd2_handle_t server;         /**< required: the service layer to feed     */
    uint8_t            ecu_addr;       /**< 0 -> 0x10; 0x11 is 0x7E1 / 0x7E9, etc.  */
    uint8_t            tester_addr;    /**< 0 -> 0xF1                               */
    uint16_t           max_msg_bytes;  /**< 0 -> 128, which covers the longest list */
} e32p_obd2_isotp_config_t;

/**
 * @brief Create both channels and start answering.
 *
 * @return ESP_OK; ESP_ERR_INVALID_ARG; ESP_ERR_INVALID_STATE when already
 *         started or the ISO-TP link is not running; ESP_ERR_NO_MEM
 */
esp_err_t e32p_obd2_isotp_start(const e32p_obd2_isotp_config_t *cfg);

/** @brief Destroy both channels. Safe to call when not started. */
esp_err_t e32p_obd2_isotp_stop(void);

/** @brief True between a successful start and a stop. */
bool e32p_obd2_isotp_running(void);

/**
 * @brief Responses dropped because the previous one was still going out.
 *
 * A scan tool asks one question at a time, so this should stay 0. It climbs if a
 * tester overlaps requests, and it is the honest way to notice that rather than
 * quietly queueing answers a tool has stopped waiting for.
 */
uint32_t e32p_obd2_isotp_dropped(void);

#ifdef __cplusplus
}
#endif
