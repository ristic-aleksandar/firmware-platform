/**
 * @file e32p_isotp.c
 * @brief ISO 15765-2 transport engine. Pure: no TWAI, no FreeRTOS, no clock.
 *
 * Frame layout (classic CAN, 8 data bytes, normal addressing):
 *
 *   SF   [0]=0x0L                       L    = 1..7 data length
 *   FF   [0]=0x1H [1]=LL                HLL  = 12-bit total length, 8..4095
 *   CF   [0]=0x2S                       S    = sequence number, wraps 15->0
 *   FC   [0]=0x3F [1]=BS [2]=STmin      F    = FlowStatus
 */
#include <string.h>
#include <stdlib.h>
#include "e32p_isotp.h"

static const char *TAG = "e32p_isotp";

#define PCI_MASK        0xF0u
#define PCI_LOW_MASK    0x0Fu
#define SF_MAX_DATA     7u
#define FF_HEAD         2u    /* PCI bytes consumed by a FirstFrame       */
#define CF_MAX_DATA     7u
#define FC_LEN          3u    /* meaningful bytes in a FlowControl frame  */
#define SN_WRAP         16u
#define DEF_TIMER_MS    1000u
#define DEF_PAD_BYTE    0xAAu
#define STMIN_RESERVED_MS 127u

typedef enum { TX_IDLE = 0, TX_WAIT_FC, TX_SENDING } tx_state_t;
typedef enum { RX_IDLE = 0, RX_ASSEMBLING } rx_state_t;

struct e32p_isotp_ctx {
    e32p_isotp_config_t cfg;

    uint32_t tx_id;          /* resolved from the addressing mode */
    uint32_t rx_id;
    bool     extended;
    uint16_t max_msg;

    uint8_t *tx_buf;
    uint8_t *rx_buf;

    /* TX session */
    tx_state_t tx_state;
    uint16_t   tx_len;
    uint16_t   tx_off;
    uint8_t    tx_sn;
    uint8_t    tx_block_left;   /* CFs remaining in this block; 0xFF = unlimited */
    uint8_t    peer_bs;
    uint8_t    peer_stmin_raw;
    uint32_t   tx_next_ms;      /* earliest time the next CF may leave */
    uint32_t   tx_deadline_ms;  /* N_Bs expiry while waiting for FC    */

    /* RX session */
    rx_state_t rx_state;
    uint16_t   rx_len;
    uint16_t   rx_off;
    uint8_t    rx_sn_expect;
    uint8_t    rx_block_count;  /* CFs taken since the last FC we sent */
    uint8_t    rx_wft_count;
    bool       rx_wait_pending; /* emitted FC.WAIT; owe the peer a CTS */
    uint32_t   rx_deadline_ms;  /* N_Cr expiry while awaiting a CF     */

    uint32_t now_ms;
    bool      have_now;
};

/* ===================== pure PCI helpers (no context) ==================== */

e32p_isotp_pci_t e32p_isotp_pci_type(const uint8_t *d, uint8_t dlc)
{
    if (d == NULL || dlc < 1u) {
        return E32P_ISOTP_PCI_BAD;
    }
    switch (d[0] & PCI_MASK) {
    case 0x00u: return E32P_ISOTP_PCI_SF;
    case 0x10u: return E32P_ISOTP_PCI_FF;
    case 0x20u: return E32P_ISOTP_PCI_CF;
    case 0x30u: return E32P_ISOTP_PCI_FC;
    default:    return E32P_ISOTP_PCI_BAD;
    }
}

esp_err_t e32p_isotp_pci_sf_len(const uint8_t *d, uint8_t dlc, uint8_t *out_len)
{
    E32P_CHECK_ARG(d != NULL && out_len != NULL, TAG);
    if (dlc < 1u || (d[0] & PCI_MASK) != 0x00u) {
        return ESP_ERR_INVALID_ARG;
    }
    const uint8_t len = d[0] & PCI_LOW_MASK;
    /* SF_DL 0 is the CAN-FD escape; on classic CAN it is invalid. The frame
       must also actually carry the bytes it claims. */
    if (len == 0u || len > SF_MAX_DATA || (uint8_t)(len + 1u) > dlc) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_len = len;
    return ESP_OK;
}

esp_err_t e32p_isotp_pci_ff_len(const uint8_t *d, uint8_t dlc, uint16_t *out_len)
{
    E32P_CHECK_ARG(d != NULL && out_len != NULL, TAG);
    if (dlc < FF_HEAD || (d[0] & PCI_MASK) != 0x10u) {
        return ESP_ERR_INVALID_ARG;
    }
    const uint16_t len = (uint16_t)(((uint16_t)(d[0] & PCI_LOW_MASK) << 8) | d[1]);
    /* FF_DL == 0 selects the 32-bit escape form (bytes 2..5). Out of scope in
       v1 — reject it rather than silently misparse a 4 GB length as zero. */
    if (len == 0u || len <= SF_MAX_DATA) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_len = len;
    return ESP_OK;
}

esp_err_t e32p_isotp_pci_cf_sn(const uint8_t *d, uint8_t dlc, uint8_t *out_sn)
{
    E32P_CHECK_ARG(d != NULL && out_sn != NULL, TAG);
    if (dlc < 1u || (d[0] & PCI_MASK) != 0x20u) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_sn = d[0] & PCI_LOW_MASK;
    return ESP_OK;
}

esp_err_t e32p_isotp_pci_fc(const uint8_t *d, uint8_t dlc, e32p_isotp_fs_t *fs,
                            uint8_t *bs, uint8_t *stmin_raw)
{
    E32P_CHECK_ARG(d != NULL && fs != NULL && bs != NULL && stmin_raw != NULL, TAG);
    if (dlc < FC_LEN || (d[0] & PCI_MASK) != 0x30u) {
        return ESP_ERR_INVALID_ARG;
    }
    const uint8_t raw_fs = d[0] & PCI_LOW_MASK;
    if (raw_fs > (uint8_t)E32P_ISOTP_FS_OVFLW) {
        return ESP_ERR_INVALID_ARG;   /* 3..15 reserved */
    }
    *fs        = (e32p_isotp_fs_t)raw_fs;
    *bs        = d[1];
    *stmin_raw = d[2];
    return ESP_OK;
}

uint32_t e32p_isotp_stmin_us(uint8_t raw)
{
    if (raw <= 0x7Fu) {
        return (uint32_t)raw * 1000u;            /* 0..127 ms */
    }
    if (raw >= 0xF1u && raw <= 0xF9u) {
        return (uint32_t)(raw - 0xF0u) * 100u;   /* 100..900 us */
    }
    return (uint32_t)STMIN_RESERVED_MS * 1000u;  /* reserved -> 127 ms */
}

uint32_t e32p_isotp_id_normal_fixed(uint8_t sa, uint8_t ta, bool functional)
{
    const uint32_t pf = functional ? 0xDBu : 0xDAu;
    return 0x18000000u | (pf << 16) | ((uint32_t)ta << 8) | (uint32_t)sa;
}

/** @brief STmin as whole milliseconds, rounded UP. Sub-millisecond separation
 *         cannot be expressed on a millisecond poll tick; sending slower than
 *         requested is always spec-legal. */
static uint32_t stmin_ms_ceil(uint8_t raw)
{
    const uint32_t us = e32p_isotp_stmin_us(raw);
    return (us + 999u) / 1000u;
}

/* ========================== frame construction ========================== */

static esp_err_t emit(struct e32p_isotp_ctx *c, uint8_t *data, uint8_t len)
{
    e32p_twai_message_t m = {
        .id       = c->tx_id,
        .extended = c->extended,
        .self     = c->cfg.self_reception,
        .len      = len,
    };
    if (c->cfg.pad_tx && len < 8u) {
        memset(&data[len], c->cfg.pad_byte, (size_t)(8u - len));
        m.len = 8u;
    }
    memcpy(m.data, data, m.len);
    return c->cfg.tx(&m, c->cfg.user);
}

static esp_err_t emit_fc(struct e32p_isotp_ctx *c, e32p_isotp_fs_t fs)
{
    uint8_t d[8] = {0};
    d[0] = (uint8_t)(0x30u | (uint8_t)fs);
    d[1] = c->cfg.rx_block_size;
    d[2] = c->cfg.rx_stmin_ms;
    return emit(c, d, FC_LEN);
}

static void tx_finish(struct e32p_isotp_ctx *c, esp_err_t status)
{
    c->tx_state = TX_IDLE;
    c->tx_len   = 0;
    c->tx_off   = 0;
    if (c->cfg.on_tx_done != NULL) {
        c->cfg.on_tx_done(status, c->cfg.user);
    }
}

static void rx_reset(struct e32p_isotp_ctx *c)
{
    c->rx_state       = RX_IDLE;
    c->rx_len         = 0;
    c->rx_off         = 0;
    c->rx_sn_expect   = 1u;   /* the CF after a FF carries SN 1 */
    c->rx_block_count = 0;
    c->rx_wft_count   = 0;
    c->rx_wait_pending = false;
}

/**
 * @brief Emit as many consecutive frames as block size, separation time and the
 *        sink currently allow. Never one per call: at STmin 0 the limit is the
 *        sink's back-pressure, not the poll cadence.
 */
static void tx_pump_cfs(struct e32p_isotp_ctx *c)
{
    const uint32_t sep_ms = stmin_ms_ceil(c->peer_stmin_raw);

    while (c->tx_state == TX_SENDING && c->tx_off < c->tx_len) {
        if (c->now_ms < c->tx_next_ms) {
            break;                       /* STmin not elapsed */
        }
        if (c->tx_block_left == 0u) {
            /* Block complete — wait for the peer's next FC. */
            c->tx_state       = TX_WAIT_FC;
            c->tx_deadline_ms = c->now_ms + c->cfg.n_bs_ms;
            return;
        }

        uint16_t chunk = (uint16_t)(c->tx_len - c->tx_off);
        if (chunk > CF_MAX_DATA) {
            chunk = CF_MAX_DATA;
        }
        uint8_t d[8];
        d[0] = (uint8_t)(0x20u | (c->tx_sn & PCI_LOW_MASK));
        memcpy(&d[1], &c->tx_buf[c->tx_off], chunk);

        if (emit(c, d, (uint8_t)(chunk + 1u)) != ESP_OK) {
            return;   /* sink full: back-pressure, resume next poll */
        }

        c->tx_off = (uint16_t)(c->tx_off + chunk);
        c->tx_sn  = (uint8_t)((c->tx_sn + 1u) % SN_WRAP);
        if (c->tx_block_left != 0xFFu) {
            c->tx_block_left--;
        }
        /* now_ms is whole milliseconds, so a CF sent at x.9 ms is stamped x.
           The extra millisecond keeps the real gap at STmin or more. */
        c->tx_next_ms = c->now_ms + ((sep_ms > 0u) ? sep_ms + 1u : 0u);

        if (c->tx_off >= c->tx_len) {
            tx_finish(c, ESP_OK);
            return;
        }
        if (sep_ms > 0u) {
            break;    /* pace the rest onto later polls */
        }
    }
}

/* ============================== public API ============================== */

void e32p_isotp_config_defaults(e32p_isotp_config_t *cfg,
                                uint32_t resp_id, uint32_t req_id,
                                uint8_t sa, uint8_t ta, bool functional)
{
    if (cfg == NULL) {
        return;
    }
    memset(cfg, 0, sizeof(*cfg));
    cfg->mode          = E32P_ISOTP_DEFAULT_ADDR_MODE;
    cfg->tx_id         = resp_id;
    cfg->rx_id         = req_id;
    cfg->source_addr   = sa;
    cfg->target_addr   = ta;
    cfg->functional    = functional;
    cfg->rx_block_size = 8u;
    cfg->rx_stmin_ms   = 1u;
    cfg->rx_wft_max    = 0u;
    cfg->pad_tx        = true;
    cfg->pad_byte      = DEF_PAD_BYTE;
    cfg->max_msg_bytes = CONFIG_E32P_ISOTP_MAX_MSG_BYTES;
    cfg->n_bs_ms       = DEF_TIMER_MS;
    cfg->n_cr_ms       = DEF_TIMER_MS;
    cfg->n_as_ms       = DEF_TIMER_MS;
}

esp_err_t e32p_isotp_init(const e32p_isotp_config_t *cfg, e32p_isotp_handle_t *out)
{
    E32P_CHECK_ARG(cfg != NULL && out != NULL, TAG);
    E32P_CHECK_ARG(cfg->tx != NULL && cfg->on_rx != NULL, TAG);

    uint16_t max_msg = cfg->max_msg_bytes ? cfg->max_msg_bytes
                                          : (uint16_t)CONFIG_E32P_ISOTP_MAX_MSG_BYTES;
    E32P_CHECK_ARG(max_msg >= 8u && max_msg <= E32P_ISOTP_MAX_PAYLOAD, TAG);
    E32P_CHECK_ARG(cfg->rx_stmin_ms <= STMIN_RESERVED_MS, TAG);

    struct e32p_isotp_ctx *c = calloc(1, sizeof(*c));
    if (c == NULL) {
        return ESP_ERR_NO_MEM;
    }
    c->tx_buf = malloc(max_msg);
    c->rx_buf = malloc(max_msg);
    if (c->tx_buf == NULL || c->rx_buf == NULL) {
        free(c->tx_buf);
        free(c->rx_buf);
        free(c);
        return ESP_ERR_NO_MEM;
    }

    c->cfg     = *cfg;
    c->max_msg = max_msg;
    c->cfg.max_msg_bytes = max_msg;
    if (c->cfg.pad_byte == 0u && c->cfg.pad_tx) {
        c->cfg.pad_byte = DEF_PAD_BYTE;
    }
    if (c->cfg.n_bs_ms == 0u) { c->cfg.n_bs_ms = DEF_TIMER_MS; }
    if (c->cfg.n_cr_ms == 0u) { c->cfg.n_cr_ms = DEF_TIMER_MS; }
    if (c->cfg.n_as_ms == 0u) { c->cfg.n_as_ms = DEF_TIMER_MS; }

    if (cfg->mode == E32P_ISOTP_ADDR_NORMAL_FIXED) {
        c->extended = true;
        c->tx_id = e32p_isotp_id_normal_fixed(cfg->source_addr, cfg->target_addr,
                                              cfg->functional);
        c->rx_id = e32p_isotp_id_normal_fixed(cfg->target_addr, cfg->source_addr,
                                              cfg->functional);
    } else {
        c->tx_id    = cfg->tx_id;
        c->rx_id    = cfg->rx_id;
        c->extended = (cfg->tx_id > 0x7FFu) || (cfg->rx_id > 0x7FFu);
    }

    rx_reset(c);
    *out = c;
    return ESP_OK;
}

esp_err_t e32p_isotp_deinit(e32p_isotp_handle_t h)
{
    if (h == NULL) {
        return ESP_OK;
    }
    free(h->tx_buf);
    free(h->rx_buf);
    free(h);
    return ESP_OK;
}

esp_err_t e32p_isotp_send(e32p_isotp_handle_t c, const uint8_t *data, uint16_t len)
{
    E32P_CHECK_ARG(c != NULL && data != NULL, TAG);
    E32P_CHECK_ARG(len > 0u && len <= c->max_msg, TAG);
    E32P_RETURN_ON_FALSE(c->tx_state == TX_IDLE, ESP_ERR_INVALID_STATE, TAG,
                         "tx session already active");

    memcpy(c->tx_buf, data, len);
    c->tx_len = len;
    c->tx_off = 0;

    if (len <= SF_MAX_DATA) {
        uint8_t d[8];
        d[0] = (uint8_t)(0x00u | (uint8_t)len);
        memcpy(&d[1], data, len);
        esp_err_t err = emit(c, d, (uint8_t)(len + 1u));
        if (err != ESP_OK) {
            c->tx_len = 0;
            return err;
        }
        c->tx_off = len;
        tx_finish(c, ESP_OK);
        return ESP_OK;
    }

    /* Multi-frame: FirstFrame then wait for the peer's FlowControl. */
    uint8_t d[8];
    d[0] = (uint8_t)(0x10u | (uint8_t)((len >> 8) & PCI_LOW_MASK));
    d[1] = (uint8_t)(len & 0xFFu);
    memcpy(&d[2], data, 8u - FF_HEAD);

    esp_err_t err = emit(c, d, 8u);
    if (err != ESP_OK) {
        c->tx_len = 0;
        return err;
    }
    c->tx_off         = 8u - FF_HEAD;
    c->tx_sn          = 1u;
    c->tx_state       = TX_WAIT_FC;
    c->tx_deadline_ms = c->now_ms + c->cfg.n_bs_ms;
    return ESP_OK;
}

static void handle_sf(struct e32p_isotp_ctx *c, const e32p_twai_message_t *f)
{
    uint8_t len;
    if (e32p_isotp_pci_sf_len(f->data, f->len, &len) != ESP_OK) {
        return;
    }
    /* A SingleFrame is self-contained; it does not disturb an active
       reassembly, but per ISO it does take precedence as a new message. */
    c->cfg.on_rx(&f->data[1], len, c->cfg.user);
}

static void handle_ff(struct e32p_isotp_ctx *c, const e32p_twai_message_t *f)
{
    uint16_t total;
    if (e32p_isotp_pci_ff_len(f->data, f->len, &total) != ESP_OK) {
        return;
    }
    rx_reset(c);

    if (total > c->max_msg) {
        (void)emit_fc(c, E32P_ISOTP_FS_OVFLW);
        return;
    }
    const uint8_t first = (uint8_t)(f->len > FF_HEAD ? f->len - FF_HEAD : 0u);
    memcpy(c->rx_buf, &f->data[FF_HEAD], first);

    c->rx_len         = total;
    c->rx_off         = first;
    c->rx_state       = RX_ASSEMBLING;
    c->rx_deadline_ms = c->now_ms + c->cfg.n_cr_ms;

    (void)emit_fc(c, E32P_ISOTP_FS_CTS);
}

static void handle_cf(struct e32p_isotp_ctx *c, const e32p_twai_message_t *f)
{
    if (c->rx_state != RX_ASSEMBLING) {
        return;                     /* stray CF, no session */
    }
    uint8_t sn;
    if (e32p_isotp_pci_cf_sn(f->data, f->len, &sn) != ESP_OK) {
        return;
    }
    if (sn != c->rx_sn_expect) {
        rx_reset(c);                /* sequence break: abort per ISO 15765-2 */
        return;
    }

    uint16_t remain = (uint16_t)(c->rx_len - c->rx_off);
    uint8_t  avail  = (uint8_t)(f->len > 1u ? f->len - 1u : 0u);
    uint16_t take   = (remain < avail) ? remain : avail;
    memcpy(&c->rx_buf[c->rx_off], &f->data[1], take);
    c->rx_off       = (uint16_t)(c->rx_off + take);
    c->rx_sn_expect = (uint8_t)((c->rx_sn_expect + 1u) % SN_WRAP);
    c->rx_deadline_ms = c->now_ms + c->cfg.n_cr_ms;

    if (c->rx_off >= c->rx_len) {
        c->cfg.on_rx(c->rx_buf, c->rx_len, c->cfg.user);
        rx_reset(c);
        return;
    }

    /* Block accounting: BS 0 means "send everything, no further FC". */
    if (c->cfg.rx_block_size != 0u) {
        c->rx_block_count++;
        if (c->rx_block_count >= c->cfg.rx_block_size) {
            c->rx_block_count = 0;
            if (c->cfg.rx_wft_max != 0u && c->rx_wft_count < c->cfg.rx_wft_max) {
                c->rx_wft_count++;
                /* Ask the peer to hold. We are always ready again by the next
                   poll, so poll() owes it a CTS - without that the sender sits
                   in TX_WAIT_FC until N_Bs and the transfer deadlocks. */
                c->rx_wait_pending = true;
                (void)emit_fc(c, E32P_ISOTP_FS_WAIT);
            } else {
                c->rx_wft_count = 0;
                (void)emit_fc(c, E32P_ISOTP_FS_CTS);
            }
        }
    }
}

static void handle_fc(struct e32p_isotp_ctx *c, const e32p_twai_message_t *f)
{
    e32p_isotp_fs_t fs;
    uint8_t bs, stmin;
    if (e32p_isotp_pci_fc(f->data, f->len, &fs, &bs, &stmin) != ESP_OK) {
        return;
    }
    if (c->tx_state != TX_WAIT_FC && c->tx_state != TX_SENDING) {
        return;                     /* stray FC */
    }

    switch (fs) {
    case E32P_ISOTP_FS_OVFLW:
        tx_finish(c, ESP_ERR_NO_MEM);
        return;
    case E32P_ISOTP_FS_WAIT:
        c->tx_deadline_ms = c->now_ms + c->cfg.n_bs_ms;   /* restart N_Bs */
        c->tx_state       = TX_WAIT_FC;
        return;
    case E32P_ISOTP_FS_CTS:
    default:
        break;
    }

    c->peer_bs         = bs;
    c->peer_stmin_raw  = stmin;
    c->tx_block_left   = (bs == 0u) ? 0xFFu : bs;
    c->tx_state        = TX_SENDING;
    /* The first CF of the block goes out at the next poll, not from here.
       on_frame() has no clock of its own: now_ms is still the previous
       poll's. Pacing the block from that old stamp let the next poll send
       CF 2 right behind CF 1 (on the bus: 12 us apart with STmin 1 ms). */
    c->tx_next_ms      = c->now_ms;
}

esp_err_t e32p_isotp_on_frame(e32p_isotp_handle_t c, const e32p_twai_message_t *f)
{
    E32P_CHECK_ARG(c != NULL && f != NULL, TAG);
    if (f->id != c->rx_id || f->extended != c->extended) {
        return ESP_OK;              /* not ours */
    }

    switch (e32p_isotp_pci_type(f->data, f->len)) {
    case E32P_ISOTP_PCI_SF: handle_sf(c, f); break;
    case E32P_ISOTP_PCI_FF: handle_ff(c, f); break;
    case E32P_ISOTP_PCI_CF: handle_cf(c, f); break;
    case E32P_ISOTP_PCI_FC: handle_fc(c, f); break;
    default: break;                 /* unknown PCI: ignore */
    }
    return ESP_OK;
}

void e32p_isotp_poll(e32p_isotp_handle_t c, uint32_t now_ms)
{
    if (c == NULL) {
        return;
    }
    if (!c->have_now) {
        /* First poll. The engine has no clock of its own, so anything armed
           before now - a send() or a received First Frame right after init -
           was stamped from time 0. Rebase those timers onto the real clock;
           otherwise a first poll at a large uptime expires them on the spot
           (on hardware: every multi-frame transfer failed with ESP_ERR_TIMEOUT
           a few ms after the channel was created). Idle timers are re-stamped
           when a session starts, so shifting them too is harmless. */
        c->tx_deadline_ms += now_ms;
        c->tx_next_ms     += now_ms;
        c->rx_deadline_ms += now_ms;
        c->have_now = true;
    }
    c->now_ms = now_ms;

    if (c->tx_state == TX_WAIT_FC && (int32_t)(now_ms - c->tx_deadline_ms) >= 0) {
        tx_finish(c, ESP_ERR_TIMEOUT);          /* N_Bs */
    }
    if (c->rx_state == RX_ASSEMBLING && (int32_t)(now_ms - c->rx_deadline_ms) >= 0) {
        rx_reset(c);                            /* N_Cr */
    }
    /* A FC.WAIT we sent last block is released here: we are ready again. */
    if (c->rx_state == RX_ASSEMBLING && c->rx_wait_pending) {
        c->rx_wait_pending = false;
        (void)emit_fc(c, E32P_ISOTP_FS_CTS);
    }
    if (c->tx_state == TX_SENDING) {
        tx_pump_cfs(c);
    }
}

bool e32p_isotp_tx_busy(e32p_isotp_handle_t c)
{
    return c != NULL && c->tx_state != TX_IDLE;
}

bool e32p_isotp_rx_busy(e32p_isotp_handle_t c)
{
    return c != NULL && c->rx_state != RX_IDLE;
}

uint32_t e32p_isotp_rx_id(e32p_isotp_handle_t c)
{
    return (c != NULL) ? c->rx_id : 0u;
}

bool e32p_isotp_is_extended(e32p_isotp_handle_t c)
{
    return c != NULL && c->extended;
}
