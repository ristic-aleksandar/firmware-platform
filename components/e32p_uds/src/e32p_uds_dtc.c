/**
 * @file e32p_uds_dtc.c
 * @brief e32p_uds 0x19 ReadDTCInformation and 0x14 ClearDiagnosticInformation,
 *        over the application's DTC provider.
 *
 * Report types: 0x01 count by status mask, 0x02 list by status mask, 0x0A all
 * supported codes. The provider only lists codes; the matching rule is here, so it
 * is the same for every application: a code matches when its status AND the
 * request's mask AND the availability mask is non-zero (ISO 14229-1). Reported
 * status bytes carry only the bits the availability mask says the ECU supports.
 */
#include <string.h>
#include "e32p_uds_priv.h"

#define REPORT_COUNT_BY_MASK 0x01u
#define REPORT_BY_MASK       0x02u
#define REPORT_SUPPORTED     0x0Au
#define GROUP_ALL            0xFFFFFFu

/** @brief Append one code and its status: DTC high, middle, low byte, then status. */
static bool append(struct e32p_uds_ctx *c, uint16_t *pos, uint32_t dtc, uint8_t status)
{
    if ((uint32_t)*pos + 4u > UDS_RESP_MAX) {
        return false;
    }
    c->resp[(*pos)++] = (uint8_t)(dtc >> 16);
    c->resp[(*pos)++] = (uint8_t)(dtc >> 8);
    c->resp[(*pos)++] = (uint8_t)dtc;
    c->resp[(*pos)++] = status;
    return true;
}

e32p_uds_nrc_t uds_read_dtc_info(struct e32p_uds_ctx *c, const uint8_t *req, uint16_t len,
                                 uint16_t *resp_len)
{
    const e32p_uds_dtc_provider_t *p = c->cfg.dtc;
    const uint8_t type = (uint8_t)(req[1] & 0x7Fu);
    const uint16_t want_len = (type == REPORT_SUPPORTED) ? 2u : 3u;
    if (len != want_len) {
        return E32P_UDS_NRC_BAD_LENGTH;
    }
    /* 0x0A lists every code the ECU knows, whatever its status; the others filter. */
    const uint8_t mask = (type == REPORT_SUPPORTED) ? 0u : (uint8_t)(req[2] & p->availability_mask);
    const uint16_t total = p->count(c->cfg.user);

    uint16_t pos = 0u;
    c->resp[pos++] = (uint8_t)(UDS_SID_READ_DTC + 0x40u);
    c->resp[pos++] = type;
    c->resp[pos++] = p->availability_mask;
    if (type == REPORT_COUNT_BY_MASK) {
        c->resp[pos++] = p->format;
        pos = (uint16_t)(pos + 2u);   /* the count goes here, once known */
    }

    uint16_t matched = 0u;
    for (uint16_t i = 0u; i < total; i++) {
        uint32_t dtc = 0u;
        uint8_t status = 0u;
        if (!p->at(i, &dtc, &status, c->cfg.user)) {
            break;
        }
        if (type != REPORT_SUPPORTED && (status & mask) == 0u) {
            continue;
        }
        matched++;
        if (type != REPORT_COUNT_BY_MASK &&
            !append(c, &pos, dtc, (uint8_t)(status & p->availability_mask))) {
            return E32P_UDS_NRC_RESPONSE_TOO_LONG;
        }
    }
    if (type == REPORT_COUNT_BY_MASK) {
        c->resp[4] = (uint8_t)(matched >> 8);
        c->resp[5] = (uint8_t)matched;
    }
    *resp_len = pos;
    return E32P_UDS_NRC_OK;
}

e32p_uds_nrc_t uds_clear_dtc(struct e32p_uds_ctx *c, const uint8_t *req, uint16_t len,
                             uint16_t *resp_len)
{
    if (len != 4u) {
        return E32P_UDS_NRC_BAD_LENGTH;
    }
    const uint32_t group = ((uint32_t)req[1] << 16) | ((uint32_t)req[2] << 8) | req[3];
    if (group != GROUP_ALL) {
        return E32P_UDS_NRC_OUT_OF_RANGE;   /* only "all" is supported */
    }
    c->resp[0] = (uint8_t)(UDS_SID_CLEAR_DTC + 0x40u);
    *resp_len = 1u;
    return c->cfg.dtc->clear(group, c->cfg.user);
}
