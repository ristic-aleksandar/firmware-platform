/**
 * @file e32p_uds.c
 * @brief e32p_uds implementation — dispatch, sessions and timing.
 *
 * One table describes every service: where it may run, whether it needs security,
 * its sub-functions, and its handler. preconditions() walks the ISO 14229-1 check
 * order (ticket D-17) against that table before any handler runs, so handlers never
 * re-check and the order a tester observes is fixed in one place.
 *
 * Services: 0x10, 0x11, 0x14, 0x19, 0x22, 0x27, 0x2E, 0x31, 0x34, 0x36, 0x37, 0x3E.
 * 0x27 lives in e32p_uds_security.c, 0x14 / 0x19 in e32p_uds_dtc.c, 0x31 in
 * e32p_uds_routine.c, the download services in e32p_uds_transfer.c.
 *
 * The 0x78 engine (ticket D-16) is here too. A handler whose callback answers
 * PENDING leaves its positive-response echo in c->resp; the engine sends 7F <sid> 78
 * at once, repeats it from poll() at 80 % of P2* so a late poll still lands inside
 * P2*, answers every other supported service 0x21 meanwhile (an unsupported one stays
 * 0x11), holds S3, and gives up with 0x10
 * at the pending cap. e32p_uds_complete() runs the handler's finish step and sends
 * the final answer - always, even under suppressPosRsp.
 */
#include <string.h>
#include <stdlib.h>
#include "e32p_uds.h"
#include "e32p_uds_priv.h"

static const char *TAG = "e32p_uds";

#define NEG_RESP_SID  0x7Fu
#define POSITIVE(sid) ((uint8_t)((sid) + 0x40u))
#define SUPPRESS_POS  0x80u                      /**< suppressPosRspMsgIndicationBit */
#define SUBFUNC(b)    ((uint8_t)((b) & 0x7Fu))

#define SID_SESSION_CONTROL 0x10u
#define SID_ECU_RESET       0x11u
#define SID_READ_DID        0x22u
#define SID_WRITE_DID       0x2Eu
#define SID_TESTER_PRESENT  0x3Eu

#define RESET_HARD          0x01u
#define TP_ZERO_SUBFUNC     0x00u

#define COUNT(a) ((uint8_t)(sizeof(a) / sizeof((a)[0])))

/* ---- the service table ------------------------------------------------------ */

/** @brief One sub-function and where it may run. */
typedef struct {
    uint8_t id;          /**< sub-function value, suppress bit cleared */
    uint8_t sessions;    /**< E32P_UDS_IN_* mask                       */
    bool    secured;     /**< needs security access unlocked           */
} uds_sub_t;

/** @brief One service: its preconditions, and the handler that runs after them. */
typedef struct {
    uint8_t          sid;
    uint8_t          sessions;                             /**< E32P_UDS_IN_* mask        */
    bool             secured;                              /**< whole service needs unlock */
    const uds_sub_t *subs;                                 /**< NULL: no sub-function     */
    uint8_t          sub_count;
    bool           (*configured)(const struct e32p_uds_ctx *c);   /**< NULL: always      */
    uds_handler_t    handler;
} uds_service_t;

/* ---- helpers ---------------------------------------------------------------- */

uint8_t uds_session_mask(e32p_uds_session_t s)
{
    switch (s) {
    case E32P_UDS_SESSION_PROGRAMMING: return E32P_UDS_IN_PROGRAMMING;
    case E32P_UDS_SESSION_EXTENDED:    return E32P_UDS_IN_EXTENDED;
    default:                           return E32P_UDS_IN_DEFAULT;
    }
}

/** @brief Every session change — including into the same session — locks again. */
static void enter_session(struct e32p_uds_ctx *c, e32p_uds_session_t s)
{
    c->session = s;
    uds_security_lock(c);
    uds_transfer_reset(c);
}

/** @brief A non-default session with no request for S3 falls back to default.
 *         Never called while a request is pending: S3 is held then. */
static void s3_check(struct e32p_uds_ctx *c, uint32_t now_ms)
{
    if (c->session != E32P_UDS_SESSION_DEFAULT &&
        (uint32_t)(now_ms - c->last_request_ms) >= c->cfg.s3_ms) {
        enter_session(c, E32P_UDS_SESSION_DEFAULT);
    }
}

static const e32p_uds_did_t *find_did(const struct e32p_uds_ctx *c, uint16_t did)
{
    for (uint8_t i = 0; i < c->cfg.did_count; i++) {
        if (c->cfg.dids[i].did == did) {
            return &c->cfg.dids[i];
        }
    }
    return NULL;
}

/**
 * @brief Send 7F <sid> <nrc>. Built in its own small buffer, never in c->resp,
 *        which may hold the echo of a pending request.
 */
static void send_nrc(struct e32p_uds_ctx *c, uint8_t sid, e32p_uds_nrc_t nrc)
{
    const uint8_t msg[3] = { NEG_RESP_SID, sid, (uint8_t)nrc };
    (void)c->cfg.send(msg, sizeof(msg), c->cfg.user);
}

/**
 * @brief Send a negative response — or nothing, when the request was functional
 *        and the NRC is one ISO 14229-1 suppresses for broadcasts (D-18).
 */
static void negative(struct e32p_uds_ctx *c, e32p_uds_addressing_t addressing, uint8_t sid,
                     e32p_uds_nrc_t nrc)
{
    if (addressing == E32P_UDS_ADDR_FUNCTIONAL &&
        (nrc == E32P_UDS_NRC_SERVICE_NOT_SUPPORTED || nrc == E32P_UDS_NRC_SUBFUNC_NOT_SUPPORTED ||
         nrc == E32P_UDS_NRC_OUT_OF_RANGE || nrc == E32P_UDS_NRC_SUBFUNC_NOT_IN_SESSION ||
         nrc == E32P_UDS_NRC_SERVICE_NOT_IN_SESSION)) {
        return;
    }
    send_nrc(c, sid, nrc);
}

static bool is_download_sid(uint8_t sid)
{
    return sid == UDS_SID_REQUEST_DOWNLOAD || sid == UDS_SID_TRANSFER_DATA ||
           sid == UDS_SID_TRANSFER_EXIT;
}

/** @brief Run the request's finish step, if it has one, with its outcome. */
static void run_finish(struct e32p_uds_ctx *c, uds_finish_t finish, e32p_uds_nrc_t outcome)
{
    if (finish != NULL) {
        finish(c, outcome);
    }
}

/* ---- services --------------------------------------------------------------- */

static bool has_reset(const struct e32p_uds_ctx *c)
{
    return c->cfg.on_reset != NULL;
}

static bool has_did_read(const struct e32p_uds_ctx *c)
{
    return c->cfg.dids != NULL && c->cfg.did_count > 0u && c->cfg.on_did_read != NULL;
}

static bool has_did_write(const struct e32p_uds_ctx *c)
{
    return c->cfg.dids != NULL && c->cfg.did_count > 0u && c->cfg.on_did_write != NULL;
}

static bool has_security(const struct e32p_uds_ctx *c)
{
    return c->cfg.on_seed != NULL && c->cfg.on_key != NULL;
}

static bool has_dtc_read(const struct e32p_uds_ctx *c)
{
    return c->cfg.dtc != NULL && c->cfg.dtc->count != NULL && c->cfg.dtc->at != NULL;
}

static bool has_dtc_clear(const struct e32p_uds_ctx *c)
{
    return c->cfg.dtc != NULL && c->cfg.dtc->clear != NULL;
}

static bool has_routines(const struct e32p_uds_ctx *c)
{
    return c->cfg.routines != NULL && c->cfg.routine_count > 0u && c->cfg.on_routine != NULL;
}

static bool has_sink(const struct e32p_uds_ctx *c)
{
    const e32p_uds_sink_t *k = c->cfg.sink;
    return k != NULL && k->begin != NULL && k->write != NULL && k->end != NULL;
}

/** @brief 0x10: switch session; the response carries P2 (ms) and P2* (10 ms units). */
static e32p_uds_nrc_t h_session(struct e32p_uds_ctx *c, const uint8_t *req, uint16_t len,
                                uint16_t *resp_len)
{
    if (len != 2u) {
        return E32P_UDS_NRC_BAD_LENGTH;
    }
    const uint8_t session = SUBFUNC(req[1]);
    enter_session(c, (e32p_uds_session_t)session);
    const uint16_t p2_star_10ms = (uint16_t)(c->cfg.p2_star_ms / 10u);
    c->resp[0] = POSITIVE(SID_SESSION_CONTROL);
    c->resp[1] = session;
    c->resp[2] = (uint8_t)(c->cfg.p2_ms >> 8);
    c->resp[3] = (uint8_t)c->cfg.p2_ms;
    c->resp[4] = (uint8_t)(p2_star_10ms >> 8);
    c->resp[5] = (uint8_t)p2_star_10ms;
    *resp_len = 6u;
    return E32P_UDS_NRC_OK;
}

/** @brief 0x11: answer first; the reset itself happens after the response is sent. */
static e32p_uds_nrc_t h_reset(struct e32p_uds_ctx *c, const uint8_t *req, uint16_t len,
                              uint16_t *resp_len)
{
    if (len != 2u) {
        return E32P_UDS_NRC_BAD_LENGTH;
    }
    c->reset_type = SUBFUNC(req[1]);
    c->resp[0] = POSITIVE(SID_ECU_RESET);
    c->resp[1] = c->reset_type;
    *resp_len = 2u;
    return E32P_UDS_NRC_OK;
}

/**
 * @brief 0x22: one or more DIDs. DIDs this ECU does not serve in the active session
 *        are skipped; only when none is left is the answer 0x31 (ISO 14229-1).
 */
static e32p_uds_nrc_t h_read_did(struct e32p_uds_ctx *c, const uint8_t *req, uint16_t len,
                                 uint16_t *resp_len)
{
    if (len < 3u || ((len - 1u) % 2u) != 0u) {
        return E32P_UDS_NRC_BAD_LENGTH;
    }
    const uint8_t here = uds_session_mask(c->session);
    uint16_t pos = 0u;
    bool any = false;
    c->resp[pos++] = POSITIVE(SID_READ_DID);
    for (uint16_t i = 1u; i < len; i = (uint16_t)(i + 2u)) {
        const uint16_t did = (uint16_t)(((uint16_t)req[i] << 8) | req[i + 1u]);
        const e32p_uds_did_t *d = find_did(c, did);
        if (d == NULL || (d->read_sessions & here) == 0u) {
            continue;
        }
        if ((uint32_t)pos + 2u + d->len > UDS_RESP_MAX) {
            return E32P_UDS_NRC_RESPONSE_TOO_LONG;
        }
        c->resp[pos++] = (uint8_t)(did >> 8);
        c->resp[pos++] = (uint8_t)did;
        memset(&c->resp[pos], 0, d->len);
        const e32p_uds_nrc_t nrc = c->cfg.on_did_read(did, &c->resp[pos], d->len, c->cfg.user);
        if (nrc == E32P_UDS_NRC_PENDING) {
            return E32P_UDS_NRC_GENERAL_REJECT;   /* reads answer at once, by contract */
        }
        if (nrc != E32P_UDS_NRC_OK) {
            return nrc;
        }
        pos = (uint16_t)(pos + d->len);
        any = true;
    }
    if (!any) {
        return E32P_UDS_NRC_OUT_OF_RANGE;
    }
    *resp_len = pos;
    return E32P_UDS_NRC_OK;
}

/**
 * @brief 0x2E, in ISO 14229-1 order: DID writable in this session (0x31), then
 *        security (0x33), then the exact data length (0x13).
 */
static e32p_uds_nrc_t h_write_did(struct e32p_uds_ctx *c, const uint8_t *req, uint16_t len,
                                  uint16_t *resp_len)
{
    if (len < 4u) {
        return E32P_UDS_NRC_BAD_LENGTH;
    }
    const uint16_t did = (uint16_t)(((uint16_t)req[1] << 8) | req[2]);
    const e32p_uds_did_t *d = find_did(c, did);
    if (d == NULL || (d->write_sessions & uds_session_mask(c->session)) == 0u) {
        return E32P_UDS_NRC_OUT_OF_RANGE;
    }
    if (d->write_secured && !c->sec.unlocked) {
        return E32P_UDS_NRC_SECURITY_DENIED;
    }
    if ((uint16_t)(len - 3u) != d->len) {
        return E32P_UDS_NRC_BAD_LENGTH;
    }
    /* The echo goes in first: a write that finishes later (slice 3.4) completes
       on top of it. */
    c->resp[0] = POSITIVE(SID_WRITE_DID);
    c->resp[1] = req[1];
    c->resp[2] = req[2];
    *resp_len = 3u;
    return c->cfg.on_did_write(did, &req[3], d->len, c->cfg.user);
}

/** @brief 0x3E: keeps a session alive; the S3 restart already happened on receipt. */
static e32p_uds_nrc_t h_tester_present(struct e32p_uds_ctx *c, const uint8_t *req, uint16_t len,
                                       uint16_t *resp_len)
{
    if (len != 2u) {
        return E32P_UDS_NRC_BAD_LENGTH;
    }
    c->resp[0] = POSITIVE(SID_TESTER_PRESENT);
    c->resp[1] = SUBFUNC(req[1]);
    *resp_len = 2u;
    return E32P_UDS_NRC_OK;
}

static const uds_sub_t k_session_subs[] = {
    { E32P_UDS_SESSION_DEFAULT,     E32P_UDS_IN_ANY, false },
    { E32P_UDS_SESSION_PROGRAMMING, E32P_UDS_IN_ANY, false },
    { E32P_UDS_SESSION_EXTENDED,    E32P_UDS_IN_ANY, false },
};
static const uds_sub_t k_reset_subs[] = { { RESET_HARD,      E32P_UDS_IN_ANY, false } };
static const uds_sub_t k_tp_subs[]    = { { TP_ZERO_SUBFUNC, E32P_UDS_IN_ANY, false } };
static const uds_sub_t k_sa_subs[]    = {
    { 0x01u, E32P_UDS_IN_ANY, false },   /* level 1: request seed */
    { 0x02u, E32P_UDS_IN_ANY, false },   /* level 1: send key     */
};

static const uds_sub_t k_dtc_subs[]   = {
    { 0x01u, E32P_UDS_IN_ANY, false },   /* count by status mask */
    { 0x02u, E32P_UDS_IN_ANY, false },   /* list by status mask  */
    { 0x0Au, E32P_UDS_IN_ANY, false },   /* all supported codes  */
};
static const uds_sub_t k_rc_subs[]    = {
    { 0x01u, E32P_UDS_IN_ANY, false },   /* start; each routine states its sessions */
    { 0x03u, E32P_UDS_IN_ANY, false },   /* request results                         */
};

/* ISO 14229-1 does not allow security access in the default session. */
#define NON_DEFAULT (E32P_UDS_IN_PROGRAMMING | E32P_UDS_IN_EXTENDED)

static const uds_service_t k_services[] = {
    { SID_SESSION_CONTROL, E32P_UDS_IN_ANY, false, k_session_subs, COUNT(k_session_subs), NULL,          h_session },
    { SID_ECU_RESET,       E32P_UDS_IN_ANY, false, k_reset_subs,   COUNT(k_reset_subs),   has_reset,     h_reset },
    { UDS_SID_CLEAR_DTC,   E32P_UDS_IN_ANY, false, NULL,           0u,                    has_dtc_clear, uds_clear_dtc },
    { UDS_SID_READ_DTC,    E32P_UDS_IN_ANY, false, k_dtc_subs,     COUNT(k_dtc_subs),     has_dtc_read,  uds_read_dtc_info },
    { SID_READ_DID,        E32P_UDS_IN_ANY, false, NULL,           0u,                    has_did_read,  h_read_did },
    { UDS_SID_SECURITY_ACCESS, NON_DEFAULT, false, k_sa_subs,      COUNT(k_sa_subs),      has_security,  uds_security_access },
    { SID_WRITE_DID,       E32P_UDS_IN_ANY, false, NULL,           0u,                    has_did_write, h_write_did },
    { UDS_SID_ROUTINE_CONTROL, E32P_UDS_IN_ANY, false, k_rc_subs,  COUNT(k_rc_subs),      has_routines,  uds_routine_control },
    { UDS_SID_REQUEST_DOWNLOAD, E32P_UDS_IN_PROGRAMMING, true,  NULL, 0u,                 has_sink,      uds_request_download },
    { UDS_SID_TRANSFER_DATA,    E32P_UDS_IN_PROGRAMMING, false, NULL, 0u,                 has_sink,      uds_transfer_data },
    { UDS_SID_TRANSFER_EXIT,    E32P_UDS_IN_PROGRAMMING, false, NULL, 0u,                 has_sink,      uds_transfer_exit },
    { SID_TESTER_PRESENT,  E32P_UDS_IN_ANY, false, k_tp_subs,      COUNT(k_tp_subs),      NULL,          h_tester_present },
};
/* 0x36 / 0x37 are not themselves secured: a download can only be active after a
   secured 0x34, and every way of losing security also abandons the download. */

static const uds_service_t *find_service(uint8_t sid)
{
    for (uint8_t i = 0; i < COUNT(k_services); i++) {
        if (k_services[i].sid == sid) {
            return &k_services[i];
        }
    }
    return NULL;
}

/** @brief Does this server offer the service at all — known, and its callbacks given? */
static bool is_supported(const struct e32p_uds_ctx *c, const uds_service_t *svc)
{
    return svc != NULL && (svc->configured == NULL || svc->configured(c));
}

/**
 * @brief The precondition chain (D-17), in the order of ISO 14229-1:2013 Figure 5:
 *        service supported, supported in this session, service security, minimum
 *        length, sub-function supported, sub-function in this session, sub-function
 *        security. The first check that fails decides the NRC.
 */
static e32p_uds_nrc_t preconditions(const struct e32p_uds_ctx *c, const uds_service_t *svc,
                                    const uint8_t *req, uint16_t len)
{
    const uint8_t here = uds_session_mask(c->session);
    if (!is_supported(c, svc)) {
        return E32P_UDS_NRC_SERVICE_NOT_SUPPORTED;
    }
    if ((svc->sessions & here) == 0u) {
        return E32P_UDS_NRC_SERVICE_NOT_IN_SESSION;
    }
    if (svc->secured && !c->sec.unlocked) {
        return E32P_UDS_NRC_SECURITY_DENIED;
    }
    if (svc->subs == NULL) {
        return E32P_UDS_NRC_OK;
    }
    if (len < 2u) {
        return E32P_UDS_NRC_BAD_LENGTH;
    }
    const uint8_t sub = SUBFUNC(req[1]);
    for (uint8_t i = 0; i < svc->sub_count; i++) {
        if (svc->subs[i].id != sub) {
            continue;
        }
        if ((svc->subs[i].sessions & here) == 0u) {
            return E32P_UDS_NRC_SUBFUNC_NOT_IN_SESSION;
        }
        if (svc->subs[i].secured && !c->sec.unlocked) {
            return E32P_UDS_NRC_SECURITY_DENIED;
        }
        return E32P_UDS_NRC_OK;
    }
    return E32P_UDS_NRC_SUBFUNC_NOT_SUPPORTED;
}

/* ---- API -------------------------------------------------------------------- */

esp_err_t e32p_uds_init(const e32p_uds_config_t *cfg, e32p_uds_handle_t *out)
{
    E32P_CHECK_ARG(cfg != NULL && out != NULL, TAG);
    E32P_CHECK_ARG(cfg->send != NULL, TAG);
    E32P_CHECK_ARG(cfg->did_count == 0u || cfg->dids != NULL, TAG);
    E32P_CHECK_ARG(cfg->routine_count == 0u || cfg->routines != NULL, TAG);
    /* One flag, two hooks: with only one of them the flag could never be read back
       (or never be saved), and the delay would be armed on a false picture. */
    E32P_CHECK_ARG((cfg->on_sec_load == NULL) == (cfg->on_sec_store == NULL), TAG);
    for (uint8_t i = 0; i < cfg->did_count; i++) {
        /* A DID must fit one response: SID, two DID bytes, then its data. */
        E32P_CHECK_ARG(cfg->dids[i].len > 0u && cfg->dids[i].len <= UDS_RESP_MAX - 3u, TAG);
    }

    struct e32p_uds_ctx *c = calloc(1, sizeof(*c));
    E32P_RETURN_ON_FALSE(c != NULL, ESP_ERR_NO_MEM, TAG, "no memory for the server");
    c->cfg = *cfg;
    if (c->cfg.p2_ms == 0u) {
        c->cfg.p2_ms = E32P_UDS_P2_DEFAULT_MS;
    }
    if (c->cfg.p2_star_ms == 0u) {
        c->cfg.p2_star_ms = E32P_UDS_P2_STAR_DEFAULT_MS;
    }
    if (c->cfg.s3_ms == 0u) {
        c->cfg.s3_ms = E32P_UDS_S3_DEFAULT_MS;
    }
    if (c->cfg.pending_max_ms == 0u) {
        c->cfg.pending_max_ms = E32P_UDS_PENDING_MAX_DEFAULT_MS;
    }
    c->session = E32P_UDS_SESSION_DEFAULT;   /* calloc: locked, no attempts */
    uds_security_init(c);                    /* ...and the start delay, unless the last access was fine */
    *out = c;
    return ESP_OK;
}

esp_err_t e32p_uds_deinit(e32p_uds_handle_t h)
{
    if (h == NULL) {
        return ESP_OK;
    }
    uds_transfer_reset(h);
    free(h);
    return ESP_OK;
}

esp_err_t e32p_uds_request(e32p_uds_handle_t h, e32p_uds_addressing_t addressing,
                           const uint8_t *req, uint16_t len, uint32_t now_ms)
{
    E32P_CHECK_ARG(h != NULL && req != NULL && len > 0u, TAG);
    h->now_ms = now_ms;
    uds_security_time(h);
    if (h->pend.active) {
        /* Busy with another request. A silent tester present is let through quietly:
           it only means "keep the session", and S3 is held anyway. A service this
           server does not offer is still 0x11 (silent on a broadcast); only a real
           one is "busy". */
        if (!is_supported(h, find_service(req[0]))) {
            negative(h, addressing, req[0], E32P_UDS_NRC_SERVICE_NOT_SUPPORTED);
        } else if (!(len == 2u && req[0] == SID_TESTER_PRESENT && req[1] == (SUPPRESS_POS | TP_ZERO_SUBFUNC))) {
            send_nrc(h, req[0], E32P_UDS_NRC_BUSY_REPEAT);
        }
        return ESP_OK;
    }
    s3_check(h, now_ms);          /* a session that has already expired does not answer */
    h->last_request_ms = now_ms;  /* any request restarts S3 */

    const uds_service_t *svc = find_service(req[0]);
    e32p_uds_nrc_t nrc = preconditions(h, svc, req, len);
    uint16_t resp_len = 0u;
    h->finish = NULL;
    if (nrc == E32P_UDS_NRC_OK) {
        nrc = svc->handler(h, req, len, &resp_len);
    }
    const uds_finish_t finish = h->finish;
    h->finish = NULL;
    if (nrc == E32P_UDS_NRC_PENDING) {
        h->pend.active = true;
        h->pend.sid = req[0];
        h->pend.echo_len = resp_len;
        h->pend.started_ms = now_ms;
        h->pend.last_78_ms = now_ms;
        h->pend.finish = finish;
        send_nrc(h, req[0], E32P_UDS_NRC_PENDING);   /* at once: well inside P2 */
        return ESP_OK;
    }
    run_finish(h, finish, nrc);
    if (nrc != E32P_UDS_NRC_OK) {
        h->reset_type = 0u;
        negative(h, addressing, req[0], nrc);
        return ESP_OK;
    }

    const bool suppress = (svc->subs != NULL) && ((req[1] & SUPPRESS_POS) != 0u);
    if (!suppress) {
        (void)h->cfg.send(h->resp, resp_len, h->cfg.user);
    }
    if (h->reset_type != 0u) {
        /* After the response, as ISO 14229-1 requires. The server returns to the
           state a reset leaves it in, whether or not the callback reboots. */
        const uint8_t type = h->reset_type;
        h->reset_type = 0u;
        enter_session(h, E32P_UDS_SESSION_DEFAULT);
        h->cfg.on_reset(type, h->cfg.user);
    }
    return ESP_OK;
}

esp_err_t e32p_uds_poll(e32p_uds_handle_t h, uint32_t now_ms)
{
    E32P_CHECK_ARG(h != NULL, TAG);
    h->now_ms = now_ms;
    uds_security_time(h);
    if (!h->pend.active) {
        s3_check(h, now_ms);
        return ESP_OK;
    }
    if ((uint32_t)(now_ms - h->pend.started_ms) >= h->cfg.pending_max_ms) {
        /* Given up on: a late complete() from the application finds nothing pending. */
        h->pend.active = false;
        if (is_download_sid(h->pend.sid)) {
            uds_transfer_reset(h);   /* the sink may be mid-operation: abort it */
        }
        send_nrc(h, h->pend.sid, E32P_UDS_NRC_GENERAL_REJECT);
        h->last_request_ms = now_ms;
        return ESP_OK;
    }
    const uint32_t repeat_ms = h->cfg.p2_star_ms - h->cfg.p2_star_ms / 5u;
    if ((uint32_t)(now_ms - h->pend.last_78_ms) >= repeat_ms) {
        send_nrc(h, h->pend.sid, E32P_UDS_NRC_PENDING);
        h->pend.last_78_ms = now_ms;
    }
    return ESP_OK;
}

esp_err_t e32p_uds_complete(e32p_uds_handle_t h, e32p_uds_nrc_t nrc,
                            const uint8_t *data, uint16_t len, uint32_t now_ms)
{
    E32P_CHECK_ARG(h != NULL, TAG);
    E32P_CHECK_ARG(len == 0u || data != NULL, TAG);
    E32P_CHECK_ARG(nrc != E32P_UDS_NRC_PENDING, TAG);
    if (!h->pend.active) {
        return ESP_ERR_INVALID_STATE;
    }
    h->now_ms = now_ms;
    uds_security_time(h);
    h->pend.active = false;
    h->last_request_ms = now_ms;   /* S3 restarts when the answer goes out */
    esp_err_t ret = ESP_OK;
    if (nrc == E32P_UDS_NRC_OK && (uint32_t)h->pend.echo_len + len > UDS_RESP_MAX) {
        nrc = E32P_UDS_NRC_RESPONSE_TOO_LONG;
        ret = ESP_ERR_INVALID_SIZE;
    }
    run_finish(h, h->pend.finish, nrc);
    if (nrc != E32P_UDS_NRC_OK) {
        send_nrc(h, h->pend.sid, nrc);   /* after a 0x78 every answer goes out */
        return ret;
    }
    if (len > 0u) {
        memcpy(&h->resp[h->pend.echo_len], data, len);
    }
    (void)h->cfg.send(h->resp, (uint16_t)(h->pend.echo_len + len), h->cfg.user);
    return ESP_OK;
}

e32p_uds_session_t e32p_uds_session(e32p_uds_handle_t h)
{
    return (h != NULL) ? h->session : E32P_UDS_SESSION_DEFAULT;
}
