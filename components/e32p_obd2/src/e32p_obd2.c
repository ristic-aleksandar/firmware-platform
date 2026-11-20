/**
 * @file e32p_obd2.c
 * @brief e32p_obd2 implementation — pure OBD-II service layer.
 *
 * One request in, one response out. No bus, no task, no clock. The caller sends
 * whatever lands in the response buffer; a length of 0 means "send nothing",
 * which is how ISO 15765-4 says an ECU answers a functional request it cannot
 * serve (see @ref negative).
 *
 * All six services are here: 0x01 current data, 0x02 freeze frame, 0x03 stored
 * codes, 0x04 clear, 0x07 pending codes, 0x09 vehicle information. The store
 * itself lives in e32p_obd2_dtc.c.
 */
#include <string.h>
#include <stdlib.h>
#include "e32p_obd2.h"
#include "e32p_obd2_priv.h"

static const char *TAG = "e32p_obd2";

/* Services. A positive response echoes the service id with 0x40 added. */
#define SID_CURRENT_DATA  0x01u
#define SID_FREEZE_FRAME  0x02u
#define SID_DTC_STORED    0x03u
#define SID_DTC_CLEAR     0x04u
#define SID_DTC_PENDING   0x07u
#define SID_VEHICLE_INFO  0x09u
#define POSITIVE(sid)     ((uint8_t)((sid) + 0x40u))

/* Negative response codes (ISO 14229-1, as used by J1979). */
#define NEG_RESP_SID              0x7Fu
#define NRC_SERVICE_NOT_SUPPORTED 0x11u
#define NRC_SUB_NOT_SUPPORTED     0x12u
#define NRC_BAD_LENGTH            0x13u

#define PID_BLOCK        0x20u   /**< PIDs covered by one supported-PID bitmask */
#define PID_OBD_STANDARD 0x1Cu   /**< answered from cfg.obd_standard            */
#define VEH_PID_VIN      0x02u   /**< mode 0x09: vehicle identification number  */
#define FRZ_PID_DTC      0x02u   /**< mode 0x02: the code that froze the frame  */
#define MASK_BYTES       4u
#define PID_DATA_MAX     E32P_OBD2_PID_DATA_MAX

/**
 * @brief Build a negative response, or stay silent.
 *
 * A functionally addressed request this ECU cannot answer gets nothing back, so
 * a broadcast is not answered by every ECU on the bus with an error
 * (ISO 15765-4). A physically addressed one gets the negative response.
 *
 * @return bytes written; 0 means the caller must send nothing
 */
static uint16_t negative(e32p_obd2_addressing_t addressing, uint8_t sid, uint8_t nrc,
                         uint8_t *resp, uint16_t cap)
{
    if (addressing == E32P_OBD2_ADDR_FUNCTIONAL || cap < 3u) {
        return 0u;
    }
    resp[0] = NEG_RESP_SID;
    resp[1] = sid;
    resp[2] = nrc;
    return 3u;
}

/** @brief Find @p pid in the table; returns its data length, or 0 if absent. */
static uint8_t table_len(const struct e32p_obd2_ctx *c, uint8_t pid)
{
    for (uint8_t i = 0; i < c->cfg.pid_count; i++) {
        if (c->cfg.pids[i].pid == pid) {
            return c->cfg.pids[i].len;
        }
    }
    return 0u;
}

/** @brief True when the table holds any PID above @p pid. */
static bool any_pid_above(const struct e32p_obd2_ctx *c, uint8_t pid)
{
    for (uint8_t i = 0; i < c->cfg.pid_count; i++) {
        if (c->cfg.pids[i].pid > pid) {
            return true;
        }
    }
    return false;
}

/**
 * @brief Fill the 4-byte supported-PID bitmask for the block above @p base.
 *
 * Bit order is the standard's: the first byte's top bit is `base + 1`, the last
 * byte's bottom bit is `base + 0x20` — and that bottom bit means "the next block
 * exists", which is how a scan tool knows to keep walking.
 */
static void mask_for(const struct e32p_obd2_ctx *c, uint8_t base, uint8_t out[MASK_BYTES])
{
    memset(out, 0, MASK_BYTES);
    for (uint8_t i = 0; i < c->cfg.pid_count; i++) {
        const uint8_t pid = c->cfg.pids[i].pid;
        if (pid > base && pid < (uint8_t)(base + PID_BLOCK)) {
            const uint8_t n = (uint8_t)(pid - base - 1u);        /* 0..30 */
            out[n / 8u] |= (uint8_t)(0x80u >> (n % 8u));
        }
    }
    if (any_pid_above(c, (uint8_t)(base + PID_BLOCK - 1u))) {
        out[MASK_BYTES - 1u] |= 0x01u;                           /* next block follows */
    }
}

/** @brief True when @p pid is one of the derived bitmask PIDs and we answer it. */
static bool is_supported_mask_pid(const struct e32p_obd2_ctx *c, uint8_t pid)
{
    if (pid == 0u) {
        return true;                       /* every OBD ECU answers PID 0x00 */
    }
    if ((pid % PID_BLOCK) != 0u) {
        return false;
    }
    return any_pid_above(c, pid);          /* 0x20 only when something lives above it */
}

/** @brief Mode 0x01 — current data. */
static uint16_t svc_current_data(struct e32p_obd2_ctx *c, e32p_obd2_addressing_t addressing,
                                 const uint8_t *req, uint16_t req_len,
                                 uint8_t *resp, uint16_t cap)
{
    /* v1 answers one PID per request. J1979 allows up to six; a scan tool that
       asks for more gets "wrong length" rather than a partial answer. */
    if (req_len != 2u) {
        return negative(addressing, SID_CURRENT_DATA, NRC_BAD_LENGTH, resp, cap);
    }
    const uint8_t pid = req[1];

    if ((pid % PID_BLOCK) == 0u) {
        if (!is_supported_mask_pid(c, pid)) {
            return negative(addressing, SID_CURRENT_DATA, NRC_SUB_NOT_SUPPORTED, resp, cap);
        }
        resp[0] = POSITIVE(SID_CURRENT_DATA);
        resp[1] = pid;
        if (pid == 0u) {
            memcpy(&resp[2], c->pid_mask, MASK_BYTES);           /* precomputed at init */
        } else {
            mask_for(c, pid, &resp[2]);
        }
        return (uint16_t)(2u + MASK_BYTES);
    }

    const uint8_t len = table_len(c, pid);
    if (len == 0u) {
        return negative(addressing, SID_CURRENT_DATA, NRC_SUB_NOT_SUPPORTED, resp, cap);
    }

    uint8_t data[PID_DATA_MAX] = {0};
    if (pid == PID_OBD_STANDARD) {
        data[0] = c->cfg.obd_standard;      /* the layer owns this one, not the app */
    } else if (c->cfg.on_read(pid, data, len, c->cfg.user) != ESP_OK) {
        /* The application cannot produce it right now: answer as unsupported
           rather than send a stale or zeroed value a tool would believe. */
        return negative(addressing, SID_CURRENT_DATA, NRC_SUB_NOT_SUPPORTED, resp, cap);
    }
    resp[0] = POSITIVE(SID_CURRENT_DATA);
    resp[1] = pid;
    memcpy(&resp[2], data, len);
    return (uint16_t)(2u + len);
}

/** @brief Modes 0x03 and 0x07 — stored and pending trouble codes. */
static uint16_t svc_dtc_list(struct e32p_obd2_ctx *c, e32p_obd2_addressing_t addressing,
                             uint8_t sid, uint16_t req_len, uint8_t *resp, uint16_t cap)
{
    if (req_len != 1u) {
        return negative(addressing, sid, NRC_BAD_LENGTH, resp, cap);
    }
    const bool want_confirmed = (sid == SID_DTC_STORED);
    uint16_t n = 2u;
    for (uint8_t i = 0; i < c->dtc_count; i++) {
        if (c->dtc[i].confirmed == want_confirmed) {
            resp[n++] = (uint8_t)(c->dtc[i].code >> 8);
            resp[n++] = (uint8_t)(c->dtc[i].code & 0xFFu);
        }
    }
    resp[0] = POSITIVE(sid);
    resp[1] = (uint8_t)((n - 2u) / 2u);      /* count of codes, the CAN form */
    return n;                                 /* an empty list is a valid answer */
}

/** @brief Mode 0x04 — clear stored codes, pending codes and the freeze frame. */
static uint16_t svc_dtc_clear(struct e32p_obd2_ctx *c, e32p_obd2_addressing_t addressing,
                              uint16_t req_len, uint8_t *resp, uint16_t cap)
{
    if (req_len != 1u) {
        return negative(addressing, SID_DTC_CLEAR, NRC_BAD_LENGTH, resp, cap);
    }
    if (e32p_obd2_dtc_clear(c) != ESP_OK) {
        /* The codes are gone from RAM but the write failed: say so rather than
           report success and hand them back after the next reset. */
        return negative(addressing, SID_DTC_CLEAR, NRC_SUB_NOT_SUPPORTED, resp, cap);
    }
    resp[0] = POSITIVE(SID_DTC_CLEAR);
    return 1u;
}

/** @brief Mode 0x02 — freeze frame, frame 0 only. */
static uint16_t svc_freeze_frame(struct e32p_obd2_ctx *c, e32p_obd2_addressing_t addressing,
                                 const uint8_t *req, uint16_t req_len,
                                 uint8_t *resp, uint16_t cap)
{
    if (req_len != 3u) {
        return negative(addressing, SID_FREEZE_FRAME, NRC_BAD_LENGTH, resp, cap);
    }
    const uint8_t pid = req[1];
    const uint8_t frame = req[2];
    if (!c->freeze_valid || frame != 0u) {
        return negative(addressing, SID_FREEZE_FRAME, NRC_SUB_NOT_SUPPORTED, resp, cap);
    }

    resp[0] = POSITIVE(SID_FREEZE_FRAME);
    resp[1] = pid;
    resp[2] = frame;
    if (pid == 0u) {
        memcpy(&resp[3], c->pid_mask, MASK_BYTES);
        resp[3] |= 0x40u;                     /* PID 0x02: the code that froze it */
        return (uint16_t)(3u + MASK_BYTES);
    }
    if (pid == FRZ_PID_DTC) {
        resp[3] = (uint8_t)(c->freeze_dtc >> 8);
        resp[4] = (uint8_t)(c->freeze_dtc & 0xFFu);
        return 5u;
    }
    const uint8_t len = table_len(c, pid);
    if (len == 0u || !e32p_obd2_freeze_get(c, pid, &resp[3], len)) {
        return negative(addressing, SID_FREEZE_FRAME, NRC_SUB_NOT_SUPPORTED, resp, cap);
    }
    return (uint16_t)(3u + len);
}

/** @brief Mode 0x09 — vehicle information. Only the VIN in v1. */
static uint16_t svc_vehicle_info(struct e32p_obd2_ctx *c, e32p_obd2_addressing_t addressing,
                                 const uint8_t *req, uint16_t req_len,
                                 uint8_t *resp, uint16_t cap)
{
    if (req_len != 2u) {
        return negative(addressing, SID_VEHICLE_INFO, NRC_BAD_LENGTH, resp, cap);
    }
    const uint8_t pid = req[1];

    if (pid == 0u && c->cfg.vin != NULL) {
        resp[0] = POSITIVE(SID_VEHICLE_INFO);
        resp[1] = 0u;
        memset(&resp[2], 0, MASK_BYTES);
        resp[2] = 0x40u;                     /* PID 0x02 (VIN) is supported */
        return (uint16_t)(2u + MASK_BYTES);
    }
    if (pid == VEH_PID_VIN && c->cfg.vin != NULL) {
        resp[0] = POSITIVE(SID_VEHICLE_INFO);
        resp[1] = VEH_PID_VIN;
        resp[2] = 1u;                        /* one data item follows */
        memcpy(&resp[3], c->cfg.vin, E32P_OBD2_VIN_LEN);
        return (uint16_t)(3u + E32P_OBD2_VIN_LEN);
    }
    return negative(addressing, SID_VEHICLE_INFO, NRC_SUB_NOT_SUPPORTED, resp, cap);
}

esp_err_t e32p_obd2_init(const e32p_obd2_config_t *cfg, e32p_obd2_handle_t *out)
{
    E32P_CHECK_ARG(cfg != NULL && out != NULL, TAG);
    E32P_CHECK_ARG(cfg->pid_count == 0u || cfg->pids != NULL, TAG);
    E32P_CHECK_ARG(cfg->pid_count == 0u || cfg->on_read != NULL, TAG);
    E32P_CHECK_ARG(cfg->vin == NULL || strlen(cfg->vin) == E32P_OBD2_VIN_LEN, TAG);
    for (uint8_t i = 0; i < cfg->pid_count; i++) {
        const uint8_t pid = cfg->pids[i].pid;
        /* Multiples of 0x20 are the derived bitmasks, and a duplicate would make
           the bitmask and the lookup disagree - both are configuration bugs. */
        E32P_CHECK_ARG((pid % PID_BLOCK) != 0u, TAG);
        E32P_CHECK_ARG(cfg->pids[i].len >= 1u && cfg->pids[i].len <= PID_DATA_MAX, TAG);
        for (uint8_t j = 0; j < i; j++) {
            E32P_CHECK_ARG(cfg->pids[j].pid != pid, TAG);
        }
    }

    struct e32p_obd2_ctx *c = calloc(1, sizeof(*c));
    if (c == NULL) {
        return ESP_ERR_NO_MEM;
    }
    c->cfg = *cfg;
    mask_for(c, 0u, c->pid_mask);          /* derived once: PID 0x00 is the hot path */

    if (cfg->pid_count > 0u) {
        c->freeze_data = calloc((size_t)cfg->pid_count, PID_DATA_MAX);
        if (c->freeze_data == NULL) {
            free(c);
            return ESP_ERR_NO_MEM;
        }
    }
    const esp_err_t err = e32p_obd2_dtc_load(c);
    if (err != ESP_OK) {
        /* A store we cannot read is a configuration or flash problem the caller
           must see: starting up with silently empty codes would hide a fault. */
        free(c->freeze_data);
        free(c);
        return err;
    }

    *out = c;
    ESP_LOGI(TAG, "obd2 server ready pids=%u vin=%s dtc_store=%s", (unsigned)cfg->pid_count,
             cfg->vin != NULL ? "yes" : "no",
             cfg->nvs_namespace != NULL ? cfg->nvs_namespace : "ram");
    return ESP_OK;
}

esp_err_t e32p_obd2_deinit(e32p_obd2_handle_t h)
{
    if (h != NULL) {
        free(h->freeze_data);
    }
    free(h);
    return ESP_OK;
}

esp_err_t e32p_obd2_request(e32p_obd2_handle_t h, e32p_obd2_addressing_t addressing,
                            const uint8_t *req, uint16_t req_len,
                            uint8_t *resp, uint16_t resp_cap, uint16_t *resp_len)
{
    E32P_CHECK_ARG(h != NULL && req != NULL && resp != NULL && resp_len != NULL, TAG);
    E32P_CHECK_ARG(resp_cap >= E32P_OBD2_RESP_MAX, TAG);

    *resp_len = 0;
    if (req_len < 1u) {
        return ESP_OK;      /* no service id: nothing to name in a negative response */
    }

    const uint8_t sid = req[0];
    switch (sid) {
    case SID_CURRENT_DATA:
        *resp_len = svc_current_data(h, addressing, req, req_len, resp, resp_cap);
        break;
    case SID_VEHICLE_INFO:
        *resp_len = svc_vehicle_info(h, addressing, req, req_len, resp, resp_cap);
        break;
    case SID_FREEZE_FRAME:
        *resp_len = svc_freeze_frame(h, addressing, req, req_len, resp, resp_cap);
        break;
    case SID_DTC_STORED:
    case SID_DTC_PENDING:
        *resp_len = svc_dtc_list(h, addressing, sid, req_len, resp, resp_cap);
        break;
    case SID_DTC_CLEAR:
        *resp_len = svc_dtc_clear(h, addressing, req_len, resp, resp_cap);
        break;
    default:
        *resp_len = negative(addressing, sid, NRC_SERVICE_NOT_SUPPORTED, resp, resp_cap);
        break;
    }
    return ESP_OK;
}
