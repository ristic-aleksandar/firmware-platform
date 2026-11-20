#pragma once
/**
 * @file hil_diag.h
 * @brief Diagnostics HIL group (ISO-TP, OBD-II and UDS), plus
 *        the frame router the COMPANION uses so the diagnostics stack and the
 *        J1939 peer can share one TWAI controller and one bus.
 *
 * Single-board tests need the CAN transceiver (their self-reception loop runs
 * through it) but no companion. Two-board tests exchange with the companion's
 * ISO-TP peer, which hil_can.c:companion_task drives through the two hooks at
 * the bottom of this header. All test functions match hil_test_fn.
 */
#include <stdbool.h>
#include <stdint.h>
#include "e32p_twai.h"
#include "hil_agent.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Single board + transceiver (TWAI NO_ACK, self-reception loop). */
hil_verdict_t hil_diag_isotp_self(char *detail, size_t n);
hil_verdict_t hil_diag_isotp_lifecycle(char *detail, size_t n);

/* Two boards + transceivers (against the companion's ISO-TP peer). */
hil_verdict_t hil_diag_isotp_sf(char *detail, size_t n);
hil_verdict_t hil_diag_isotp_mf(char *detail, size_t n);
hil_verdict_t hil_diag_isotp_big(char *detail, size_t n);
hil_verdict_t hil_diag_isotp_fcwait(char *detail, size_t n);
hil_verdict_t hil_diag_isotp_ncr(char *detail, size_t n);

/* OBD-II, two boards: the DUT asks, the companion's server answers. */
hil_verdict_t hil_diag_obd2_pid_phys(char *detail, size_t n);
hil_verdict_t hil_diag_obd2_pid_func(char *detail, size_t n);
hil_verdict_t hil_diag_obd2_unsupported(char *detail, size_t n);
hil_verdict_t hil_diag_obd2_vin(char *detail, size_t n);
hil_verdict_t hil_diag_obd2_dtc_cycle(char *detail, size_t n);
hil_verdict_t hil_diag_obd2_freeze(char *detail, size_t n);

/** @brief COMPANION: store P0133 (and so a freeze frame) on a clean slate. OBD-II
 *         has no service that sets a code, so the host seeds it this way. */
hil_verdict_t hil_diag_obd2_seed(char *detail, size_t n);

/* UDS, two boards: the DUT is the tester, the companion's server answers. */
hil_verdict_t hil_diag_uds_session(char *detail, size_t n);
hil_verdict_t hil_diag_uds_functional(char *detail, size_t n);
hil_verdict_t hil_diag_uds_security(char *detail, size_t n);
hil_verdict_t hil_diag_uds_did_write(char *detail, size_t n);
hil_verdict_t hil_diag_uds_dtc(char *detail, size_t n);
hil_verdict_t hil_diag_uds_erase(char *detail, size_t n);
hil_verdict_t hil_diag_uds_download(char *detail, size_t n);
hil_verdict_t hil_diag_uds_lockout(char *detail, size_t n);

/** @brief Ends with 11 01: the companion really restarts. The host waits for it to
 *         come back, then runs hil_diag_uds_after_reset. */
hil_verdict_t hil_diag_uds_reset(char *detail, size_t n);
hil_verdict_t hil_diag_uds_after_reset(char *detail, size_t n);

/** @brief Clears any stale flag (unlock, relock), then three wrong keys and 11 01: the
 *         companion restarts with "last access failed" saved. The host waits for it, then
 *         runs hil_diag_uds_sec_after_reset. The detail string carries the round-trip time
 *         of the first wrong key (the request that saves the flag). */
hil_verdict_t hil_diag_uds_sec_reset(char *detail, size_t n);
/** @brief Right after that restart a seed is refused (0x37); after the delay an
 *         unlock works and clears the saved flag. */
hil_verdict_t hil_diag_uds_sec_after_reset(char *detail, size_t n);

/**
 * @brief True if a raw frame belongs to the diagnostics stack, not J1939.
 *
 *  - Any 11-bit frame: J1939 is 29-bit only, so 11-bit is diagnostics by
 *    definition.
 *  - A 29-bit frame whose PF byte is 0xDA or 0xDB: ISO-TP normal-fixed
 *    addressing. SAE J1939-73 reserves those (PGN 55808 / 56064) for
 *    ISO 15765-2, and they are disjoint from every PGN this repo uses
 *    (PF 0xEA 0xEB 0xEC 0xEE 0xF0 0xFE).
 */
static inline bool hil_diag_is_isotp_frame(const e32p_twai_message_t *m)
{
    if (!m->extended) {
        return true;
    }
    const uint8_t pf = (uint8_t)((m->id >> 16) & 0xFFu);
    return pf == 0xDAu || pf == 0xDBu;
}

/** @brief COMPANION: hand a diagnostics frame to the ISO-TP peer. */
void hil_diag_companion_on_frame(const e32p_twai_message_t *m);

/** @brief COMPANION: service the ISO-TP peer's timers. Call every loop pass;
 *         the first call creates the peer. */
void hil_diag_companion_poll(uint32_t now_ms);

#ifdef __cplusplus
}
#endif
