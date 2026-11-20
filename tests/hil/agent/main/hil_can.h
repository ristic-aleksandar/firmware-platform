#pragma once
/**
 * @file hil_can.h
 * @brief CAN / J1939 HIL tests (Phase 1) and the autonomous COMPANION peer.
 *
 * Single-board tests run on the DUT alone (TWAI NO_ACK self-test / in-process
 * loopback / pure codec) — no bus. Two-board tests need a COMPANION board and
 * CAN transceivers; they use TWAI NORMAL mode and are corroborated by the
 * companion's `@EVT` stream. All test functions match hil_test_fn.
 */
#include "esp_err.h"
#include "hil_agent.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Single board (no bus needed). */
hil_verdict_t hil_can_twai_neg(char *detail, size_t n);
hil_verdict_t hil_can_twai_selftx(char *detail, size_t n);
hil_verdict_t hil_can_j1939_codec(char *detail, size_t n);
hil_verdict_t hil_can_j1939_neg(char *detail, size_t n);
hil_verdict_t hil_can_tp_loop(char *detail, size_t n);
hil_verdict_t hil_can_tp_neg(char *detail, size_t n);

/* Two boards + transceivers (DUT side; corroborated by the companion). */
hil_verdict_t hil_can_twai_xfer(char *detail, size_t n);
hil_verdict_t hil_can_j1939_single(char *detail, size_t n);
hil_verdict_t hil_can_j1939_rx(char *detail, size_t n);
hil_verdict_t hil_can_tp_bam(char *detail, size_t n);
hil_verdict_t hil_can_tp_cmdt(char *detail, size_t n);
hil_verdict_t hil_can_stress(char *detail, size_t n);

/* Address claiming (J1939-81) — single board (in-process two-engine contention). */
hil_verdict_t hil_can_acl_contend(char *detail, size_t n);

/** @brief Start the autonomous J1939 peer task (COMPANION role). Brings up TWAI
 *         NORMAL + J1939 SA 0x21, logs received frames as `@EVT`, periodically
 *         broadcasts, and reassembles TP messages. */
void hil_can_start_companion(void);

/** @brief Bring TWAI up in NORMAL mode at HIL_CAN_BITRATE (deinits first).
 *         Shared with the diagnostics group. @return ESP_OK or a driver error. */
esp_err_t hil_can_bus_up_normal(void);

/** @brief Let the TX queue drain (bounded), then stop and deinit TWAI. A frame
 *         that is only queued when the controller stops is aborted - which is
 *         exactly the last CF / FC of a segmented transfer. */
void hil_can_bus_down(void);

#ifdef __cplusplus
}
#endif
