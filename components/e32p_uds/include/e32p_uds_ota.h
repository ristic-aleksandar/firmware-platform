/**
 * @file e32p_uds_ota.h
 * @brief Real reflash for e32p_uds: a download sink and two routines that write an
 *        image into the other OTA slot through e32p_ota and make it the boot image.
 *
 * The flow a tester walks:
 *   31 01 FF00   erase the inactive slot — seconds, so answered 0x78 first
 *   34 / 36 / 37 write the image; 37 validates it and sets it to boot (0x78 again)
 *   31 01 FF01   report whether the last image was accepted (status 00) or not (01)
 *   11 01        reset: the new image boots
 * If the new image never calls e32p_ota_mark_valid(), the bootloader rolls back.
 *
 * 0x34 before a finished erase answers 0x22. The slow steps run in short-lived
 * worker tasks and report back through e32p_uds_isotp_complete(), so this adapter
 * is meant to be used with the ISO-TP glue. Needs CONFIG_E32P_OTA_ENABLE.
 *
 * Security: with the key, a tester can install any image this accepts. Unless images
 * are signed, security access is all that guards this path — item 3 of the required
 * security audit in docs/SECURITY_HARDENING.md.
 */
#pragma once

#include <stdint.h>
#include "e32p_uds.h"

#ifdef __cplusplus
extern "C" {
#endif

#define E32P_UDS_RID_ERASE_MEMORY      0xFF00u
#define E32P_UDS_RID_CHECK_PROGRAMMING 0xFF01u

/** @brief The sink for e32p_uds_config_t::sink. Capacity is the inactive slot's size. */
const e32p_uds_sink_t *e32p_uds_ota_sink(void);

/**
 * @brief Routine handler for E32P_UDS_RID_ERASE_MEMORY and
 *        E32P_UDS_RID_CHECK_PROGRAMMING, with the e32p_uds_routine_fn signature:
 *        call it from your on_routine for those two IDs. List both in your routine
 *        table for the programming session, secured.
 */
e32p_uds_nrc_t e32p_uds_ota_routine(uint16_t rid, uint8_t control, const uint8_t *option,
                                    uint16_t option_len, uint8_t *status, uint16_t status_cap,
                                    uint16_t *status_len, void *user);

#ifdef __cplusplus
}
#endif
