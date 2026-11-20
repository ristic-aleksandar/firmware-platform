/**
 * @file e32p_uds_security.c
 * @brief e32p_uds 0x27 SecurityAccess — seed/key through callbacks, attempt
 *        counter, lockout delay.
 *
 * Level 1 only: 0x01 asks for a seed, 0x02 sends the key. The algorithm and its
 * secret are never here: on_seed makes the seed and on_key judges the key, both in
 * the application (ticket P3.2, decision 2).
 *
 * Rules, from ISO 14229-1:
 *  - a seed answers exactly one key, right or wrong; a key with no seed is 0x24;
 *  - an already unlocked server answers a seed request with an all-zero seed;
 *  - a wrong key is 0x35, and the E32P_UDS_SECURITY_MAX_ATTEMPTS-th in a row is
 *    0x36 and starts E32P_UDS_SECURITY_DELAY_MS, during which a seed request is 0x37;
 *  - after a start the same delay runs if the last access failed, or if the server
 *    cannot tell (no on_sec_load hook). The wrong-key count stays in RAM.
 */
#include <string.h>
#include "e32p_uds_priv.h"

#define SA_REQUEST_SEED 0x01u
#define SA_SEND_KEY     0x02u

void uds_security_lock(struct e32p_uds_ctx *c)
{
    /* The attempt counter and the lockout stay: if a session change cleared them,
       a tester could brute-force the key by switching sessions between tries. */
    c->sec.unlocked = false;
    c->sec.seed_issued = false;
}

void uds_security_init(struct e32p_uds_ctx *c)
{
    /* No hook: the server cannot tell whether the last access failed, so it assumes
       it did (ISO 14229-1). A hook that says "failed" arms it too. */
    bool failed = true;
    if (c->cfg.on_sec_load != NULL) {
        failed = c->cfg.on_sec_load(c->cfg.user);
        /* Only "not failed" is trusted as what is stored. A "failed" can also mean the
           hook could not read it, so the saved value stays unknown and the first wrong
           key saves it again. */
        c->sec.flag_known = !failed;
        c->sec.flag_failed = false;
    }
    if (failed) {
        c->sec.delay_active = true;
        c->sec.delay_start_pending = true;   /* the clock restarts at boot: start it later */
    }
}

void uds_security_time(struct e32p_uds_ctx *c)
{
    if (c->sec.delay_start_pending) {
        c->sec.delay_start_pending = false;
        c->sec.delay_start_ms = c->now_ms;
    }
}

/** @brief Save "the last access failed" through the hook, when the value changed or the
 *         saved value is unknown. A save that reports failure makes the saved value
 *         unknown (the write may have landed anyway), so the next outcome saves again. */
static void remember_outcome(struct e32p_uds_ctx *c, bool failed)
{
    if (c->cfg.on_sec_store == NULL || (c->sec.flag_known && c->sec.flag_failed == failed)) {
        return;
    }
    c->sec.flag_known = c->cfg.on_sec_store(failed, c->cfg.user);
    c->sec.flag_failed = failed;
}

/** @brief Is the lockout still running? Ends it, lazily, once its time is up. */
static bool lockout_running(struct e32p_uds_ctx *c)
{
    if (c->sec.delay_active &&
        (uint32_t)(c->now_ms - c->sec.delay_start_ms) >= E32P_UDS_SECURITY_DELAY_MS) {
        c->sec.delay_active = false;
    }
    return c->sec.delay_active;
}

static e32p_uds_nrc_t request_seed(struct e32p_uds_ctx *c, uint8_t sub, uint16_t len,
                                   uint16_t *resp_len)
{
    if (len != 2u) {
        return E32P_UDS_NRC_BAD_LENGTH;
    }
    if (lockout_running(c)) {
        return E32P_UDS_NRC_DELAY_NOT_EXPIRED;
    }
    c->resp[0] = (uint8_t)(UDS_SID_SECURITY_ACCESS + 0x40u);
    c->resp[1] = sub;
    *resp_len = (uint16_t)(2u + E32P_UDS_SECURITY_LEN);
    if (c->sec.unlocked) {
        memset(&c->resp[2], 0, E32P_UDS_SECURITY_LEN);   /* already unlocked: zero seed */
        c->sec.seed_issued = false;
        return E32P_UDS_NRC_OK;
    }
    const uint8_t level = (uint8_t)((sub + 1u) / 2u);
    const e32p_uds_nrc_t nrc = c->cfg.on_seed(level, c->sec.seed, E32P_UDS_SECURITY_LEN, c->cfg.user);
    if (nrc == E32P_UDS_NRC_PENDING) {
        return E32P_UDS_NRC_GENERAL_REJECT;   /* seeds answer at once, by contract */
    }
    if (nrc != E32P_UDS_NRC_OK) {
        return nrc;
    }
    c->sec.seed_issued = true;
    memcpy(&c->resp[2], c->sec.seed, E32P_UDS_SECURITY_LEN);
    return E32P_UDS_NRC_OK;
}

static e32p_uds_nrc_t send_key(struct e32p_uds_ctx *c, uint8_t sub, const uint8_t *req,
                               uint16_t len, uint16_t *resp_len)
{
    if (len != 2u + E32P_UDS_SECURITY_LEN) {
        return E32P_UDS_NRC_BAD_LENGTH;
    }
    if (!c->sec.seed_issued) {
        return E32P_UDS_NRC_SEQUENCE_ERROR;
    }
    c->sec.seed_issued = false;   /* this seed is spent, whatever the key */
    const uint8_t level = (uint8_t)(sub / 2u);
    if (c->cfg.on_key(level, c->sec.seed, &req[2], E32P_UDS_SECURITY_LEN, c->cfg.user)) {
        c->sec.unlocked = true;
        c->sec.failed_attempts = 0u;
        remember_outcome(c, false);
        c->resp[0] = (uint8_t)(UDS_SID_SECURITY_ACCESS + 0x40u);
        c->resp[1] = sub;
        *resp_len = 2u;
        return E32P_UDS_NRC_OK;
    }
    c->sec.failed_attempts++;
    remember_outcome(c, true);
    if (c->sec.failed_attempts >= E32P_UDS_SECURITY_MAX_ATTEMPTS) {
        c->sec.failed_attempts = 0u;
        c->sec.delay_active = true;
        c->sec.delay_start_ms = c->now_ms;
        return E32P_UDS_NRC_EXCEEDED_ATTEMPTS;
    }
    return E32P_UDS_NRC_INVALID_KEY;
}

e32p_uds_nrc_t uds_security_access(struct e32p_uds_ctx *c, const uint8_t *req, uint16_t len,
                                   uint16_t *resp_len)
{
    const uint8_t sub = (uint8_t)(req[1] & 0x7Fu);
    /* The precondition chain lets only the two level-1 sub-functions through. */
    if (sub == SA_REQUEST_SEED) {
        return request_seed(c, sub, len, resp_len);
    }
    return send_key(c, sub, req, len, resp_len);
}
