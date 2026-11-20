/**
 * @file e32p_uds_routine.c
 * @brief e32p_uds 0x31 RoutineControl over the application's routine table.
 *
 * 0x01 start and 0x03 request results; 0x02 stop is not offered. Each routine
 * states where it may run and whether it needs security, in ISO 14229-1 order:
 * routine known and allowed in this session (else 0x31), then security (0x33).
 * Whether results exist yet is the application's call (it answers 0x24 if not).
 * A request for results (0x03) carries no option record: extra bytes answer 0x13,
 * checked after the 0x31 and 0x33 checks and before the application is called.
 */
#include <string.h>
#include "e32p_uds_priv.h"

e32p_uds_nrc_t uds_routine_control(struct e32p_uds_ctx *c, const uint8_t *req, uint16_t len,
                                   uint16_t *resp_len)
{
    if (len < 4u) {
        return E32P_UDS_NRC_BAD_LENGTH;
    }
    const uint16_t rid = (uint16_t)(((uint16_t)req[2] << 8) | req[3]);
    const e32p_uds_routine_t *r = NULL;
    for (uint8_t i = 0; i < c->cfg.routine_count; i++) {
        if (c->cfg.routines[i].rid == rid) {
            r = &c->cfg.routines[i];
            break;
        }
    }
    if (r == NULL || (r->sessions & uds_session_mask(c->session)) == 0u) {
        return E32P_UDS_NRC_OUT_OF_RANGE;
    }
    if (r->secured && !c->sec.unlocked) {
        return E32P_UDS_NRC_SECURITY_DENIED;
    }

    const uint8_t control = (uint8_t)(req[1] & 0x7Fu);
    if (control == 0x03u && len != 4u) {
        return E32P_UDS_NRC_BAD_LENGTH;   /* results take no option record */
    }
    /* The echo goes in first, so a routine that finishes later (slice 3.4)
       completes on top of it. */
    c->resp[0] = (uint8_t)(UDS_SID_ROUTINE_CONTROL + 0x40u);
    c->resp[1] = control;
    c->resp[2] = req[2];
    c->resp[3] = req[3];
    uint16_t status_len = 0u;
    const e32p_uds_nrc_t nrc = c->cfg.on_routine(rid, control, &req[4], (uint16_t)(len - 4u),
                                                 &c->resp[4], (uint16_t)(UDS_RESP_MAX - 4u),
                                                 &status_len, c->cfg.user);
    if (status_len > UDS_RESP_MAX - 4u) {
        return E32P_UDS_NRC_GENERAL_REJECT;   /* the callback overran its buffer claim */
    }
    *resp_len = (uint16_t)(4u + status_len);
    return nrc;
}
