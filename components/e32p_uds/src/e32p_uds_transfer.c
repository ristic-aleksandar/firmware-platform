/**
 * @file e32p_uds_transfer.c
 * @brief e32p_uds 0x34 / 0x36 / 0x37 download — block sequence counter, byte
 *        accounting, and the sink interface (ticket D-21).
 *
 * The server owns the bookkeeping; the sink only stores bytes. Each handler names a
 * finish step that commits its state change only once the outcome is known — at
 * once, or at e32p_uds_complete() when the sink answered PENDING — so a write that
 * fails later never advances the counter.
 *
 * Rules, from ISO 14229-1:
 *  - 0x34: data format 0x00 only, 1-4 address and size bytes, address 0, size not
 *    zero (else 0x31); a download already under way is 0x22; too big is 0x70. The
 *    answer advertises E32P_UDS_MAX_BLOCK_LEN.
 *  - 0x36: the counter starts at 0x01 and wraps 0xFF to 0x00; a repeat of the last
 *    accepted block is acknowledged without writing it again; a skipped counter is
 *    0x73; more bytes than announced are 0x71; no download is 0x24.
 *  - 0x37: before all bytes arrived is 0x24; an image the sink rejects ends the
 *    download (the sink is aborted) and answers the sink's NRC.
 */
#include <string.h>
#include "e32p_uds_priv.h"

#define DATA_FORMAT_PLAIN   0x00u   /* no compression, no encryption */
#define MAX_BLOCK_FORMAT    0x20u   /* maxNumberOfBlockLength is 2 bytes */

void uds_transfer_reset(struct e32p_uds_ctx *c)
{
    if (c->xfer.state != UDS_XFER_IDLE && c->cfg.sink != NULL && c->cfg.sink->abort != NULL) {
        c->cfg.sink->abort(c->cfg.user);
    }
    memset(&c->xfer, 0, sizeof(c->xfer));
}

static uint32_t big_endian(const uint8_t *p, uint8_t n)
{
    uint32_t v = 0u;
    for (uint8_t i = 0; i < n; i++) {
        v = (v << 8) | p[i];
    }
    return v;
}

/* ---- 0x34 ------------------------------------------------------------------- */

static void finish_download(struct e32p_uds_ctx *c, e32p_uds_nrc_t outcome)
{
    if (outcome != E32P_UDS_NRC_OK) {
        c->xfer.state = UDS_XFER_IDLE;   /* the sink refused: nothing to abort */
        return;
    }
    c->xfer.state = UDS_XFER_ACTIVE;
    c->xfer.received = 0u;
    c->xfer.next_counter = 0x01u;
    c->xfer.have_last = false;
}

e32p_uds_nrc_t uds_request_download(struct e32p_uds_ctx *c, const uint8_t *req, uint16_t len,
                                    uint16_t *resp_len)
{
    if (len < 3u) {
        return E32P_UDS_NRC_BAD_LENGTH;
    }
    const uint8_t size_len = (uint8_t)(req[2] >> 4);
    const uint8_t addr_len = (uint8_t)(req[2] & 0x0Fu);
    if (size_len < 1u || size_len > 4u || addr_len < 1u || addr_len > 4u) {
        return E32P_UDS_NRC_OUT_OF_RANGE;
    }
    if (len != 3u + addr_len + size_len) {
        return E32P_UDS_NRC_BAD_LENGTH;
    }
    const uint32_t addr = big_endian(&req[3], addr_len);
    const uint32_t size = big_endian(&req[3 + addr_len], size_len);
    if (req[1] != DATA_FORMAT_PLAIN || addr != 0u || size == 0u) {
        return E32P_UDS_NRC_OUT_OF_RANGE;
    }
    if (c->xfer.state != UDS_XFER_IDLE) {
        return E32P_UDS_NRC_CONDITIONS_NOT_CORRECT;   /* one download at a time */
    }
    if (size > c->cfg.sink->capacity) {
        return E32P_UDS_NRC_DOWNLOAD_NOT_ACCEPTED;
    }
    c->xfer.state = UDS_XFER_STARTING;
    c->xfer.expected = size;
    c->resp[0] = (uint8_t)(UDS_SID_REQUEST_DOWNLOAD + 0x40u);
    c->resp[1] = MAX_BLOCK_FORMAT;
    c->resp[2] = (uint8_t)(E32P_UDS_MAX_BLOCK_LEN >> 8);
    c->resp[3] = (uint8_t)E32P_UDS_MAX_BLOCK_LEN;
    *resp_len = 4u;
    c->finish = finish_download;
    return c->cfg.sink->begin(size, c->cfg.user);
}

/* ---- 0x36 ------------------------------------------------------------------- */

static void finish_block(struct e32p_uds_ctx *c, e32p_uds_nrc_t outcome)
{
    if (outcome != E32P_UDS_NRC_OK) {
        return;   /* nothing counted: the same block may be sent again */
    }
    c->xfer.received += c->xfer.block_len;
    c->xfer.last_counter = c->xfer.block_counter;
    c->xfer.have_last = true;
    c->xfer.next_counter = (uint8_t)(c->xfer.block_counter + 1u);   /* 0xFF wraps to 0x00 */
}

e32p_uds_nrc_t uds_transfer_data(struct e32p_uds_ctx *c, const uint8_t *req, uint16_t len,
                                 uint16_t *resp_len)
{
    if (len < 3u || len > E32P_UDS_MAX_BLOCK_LEN) {
        return E32P_UDS_NRC_BAD_LENGTH;
    }
    if (c->xfer.state != UDS_XFER_ACTIVE) {
        return E32P_UDS_NRC_SEQUENCE_ERROR;
    }
    const uint8_t counter = req[1];
    c->resp[0] = (uint8_t)(UDS_SID_TRANSFER_DATA + 0x40u);
    c->resp[1] = counter;
    *resp_len = 2u;
    if (c->xfer.have_last && counter == c->xfer.last_counter) {
        return E32P_UDS_NRC_OK;   /* the tester missed our answer: acknowledge again */
    }
    if (counter != c->xfer.next_counter) {
        return E32P_UDS_NRC_WRONG_BLOCK_COUNTER;
    }
    const uint16_t n = (uint16_t)(len - 2u);
    if (c->xfer.received + n > c->xfer.expected) {
        return E32P_UDS_NRC_TRANSFER_SUSPENDED;
    }
    c->xfer.block_counter = counter;
    c->xfer.block_len = n;
    c->finish = finish_block;
    return c->cfg.sink->write(&req[2], n, c->cfg.user);
}

/* ---- 0x37 ------------------------------------------------------------------- */

static void finish_exit(struct e32p_uds_ctx *c, e32p_uds_nrc_t outcome)
{
    if (outcome != E32P_UDS_NRC_OK) {
        uds_transfer_reset(c);   /* a rejected image ends the download */
        return;
    }
    memset(&c->xfer, 0, sizeof(c->xfer));   /* done: the sink kept the image */
}

e32p_uds_nrc_t uds_transfer_exit(struct e32p_uds_ctx *c, const uint8_t *req, uint16_t len,
                                 uint16_t *resp_len)
{
    (void)req;
    (void)len;   /* an optional parameter record is accepted and ignored */
    if (c->xfer.state != UDS_XFER_ACTIVE || c->xfer.received < c->xfer.expected) {
        return E32P_UDS_NRC_SEQUENCE_ERROR;
    }
    c->resp[0] = (uint8_t)(UDS_SID_TRANSFER_EXIT + 0x40u);
    *resp_len = 1u;
    c->finish = finish_exit;
    return c->cfg.sink->end(c->cfg.user);
}
