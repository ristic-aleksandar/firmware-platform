/**
 * @file e32p_j1939tp.c
 * @brief e32p_j1939tp implementation — J1939-21 Transport Protocol engine.
 *
 * One TX session (BAM or connection-mode) plus a small pool of RX sessions.
 * Frames leave through the injected tx sink; time is supplied via poll(now_ms).
 */
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "e32p_j1939tp.h"

static const char *TAG = "e32p_j1939tp";

/* --- Tunables (Kconfig, with defensive fallbacks) ------------------------- */
#ifndef CONFIG_E32P_J1939TP_MAX_MSG_BYTES
#define CONFIG_E32P_J1939TP_MAX_MSG_BYTES 1785
#endif
#ifndef CONFIG_E32P_J1939TP_MAX_RX_SESSIONS
#define CONFIG_E32P_J1939TP_MAX_RX_SESSIONS 2
#endif
#define TP_MAX_MSG   CONFIG_E32P_J1939TP_MAX_MSG_BYTES
#define TP_MAX_RX    CONFIG_E32P_J1939TP_MAX_RX_SESSIONS
#define TP_MIN_MSG   9u     /* smallest message that needs TP (8 B fits a single frame) */
#define TP_CTS_BURST 16u    /* packets granted per CTS on the receive side */

/* --- J1939-21 protocol constants ----------------------------------------- */
#define TP_CTRL_RTS   0x10u
#define TP_CTRL_CTS   0x11u
#define TP_CTRL_EOMA  0x13u
#define TP_CTRL_BAM   0x20u
#define TP_CTRL_ABORT 0xFFu

#define TP_ABORT_BUSY      1u  /* already in a session, cannot support another */
#define TP_ABORT_RESOURCES 2u  /* resources needed for another task            */
#define TP_ABORT_TIMEOUT   3u  /* a timeout occurred                           */
#define TP_ABORT_CTS_XFER  4u  /* unexpected CTS during transfer               */

#define TP_PRIO_DEFAULT 7u
#define TP_DT_PAYLOAD   7u     /* data bytes per TP.DT packet                  */
#define TP_BAM_PACE_MS  50u    /* inter-packet spacing for a BAM sender        */
#define TP_T1_MS        750u   /* receiver: max gap between TP.DT packets       */
#define TP_T3_MS        1250u  /* sender: max wait for CTS / EndOfMsgAck        */

/* --- Session state -------------------------------------------------------- */

typedef enum {
    TX_IDLE = 0,
    TX_BAM,          /* pacing BAM data packets            */
    TX_RTS_WAIT,     /* RTS sent, waiting for first CTS    */
    TX_CMDT_SEND,    /* sending the CTS-granted burst      */
    TX_CMDT_WAIT,    /* burst done, waiting CTS / EOMA     */
} tx_state_t;

typedef struct {
    tx_state_t state;
    uint32_t   pgn;
    uint8_t    prio;
    uint8_t    dst;
    uint8_t   *buf;          /* TP_MAX_MSG bytes */
    uint16_t   len;
    uint16_t   total_packets;
    uint16_t   next_seq;     /* next TP.DT sequence to send (1-based) */
    uint16_t   burst_left;   /* CMDT: packets left in current grant   */
    uint32_t   bam_next_ms;  /* BAM: time to emit the next packet     */
    uint32_t   timer_ms;     /* CMDT: wait deadline reference          */
    bool       timer_set;
} tx_session_t;

typedef enum { RX_FREE = 0, RX_BAM, RX_CMDT } rx_kind_t;

typedef struct {
    rx_kind_t kind;
    uint8_t   src;
    uint8_t   dst;
    uint32_t  pgn;
    uint16_t  size;          /* declared total byte count */
    uint16_t  total_packets;
    uint16_t  received;      /* distinct packets stored   */
    uint16_t  burst_end;     /* CMDT: highest seq granted so far */
    uint8_t  *buf;           /* TP_MAX_MSG bytes */
    uint8_t   seen[32];      /* 256-bit received bitmap   */
    uint32_t  last_ms;       /* last progress (T1)        */
} rx_session_t;

struct e32p_j1939tp_ctx {
    uint8_t                src_addr;
    e32p_j1939tp_tx_fn     tx;
    e32p_j1939tp_rx_fn     on_rx;
    e32p_j1939tp_txdone_fn on_tx_done;
    void                  *user;
    uint32_t               now_ms;   /* last poll() timestamp, for RX timers */
    tx_session_t           txs;
    rx_session_t           rx[TP_MAX_RX];
};

/* --- Small helpers -------------------------------------------------------- */

static inline void put_pgn(uint8_t *b, uint32_t pgn)
{
    b[0] = (uint8_t)(pgn & 0xFFu);
    b[1] = (uint8_t)((pgn >> 8) & 0xFFu);
    b[2] = (uint8_t)((pgn >> 16) & 0xFFu);
}
static inline uint32_t get_pgn(const uint8_t *b)
{
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16);
}
static inline uint16_t packets_for(uint16_t len)
{
    return (uint16_t)((len + TP_DT_PAYLOAD - 1) / TP_DT_PAYLOAD);
}
/* One size rule for both directions: 9..TP_MAX_MSG bytes. */
static inline bool tp_len_ok(uint16_t len)
{
    return (len >= TP_MIN_MSG) && (len <= TP_MAX_MSG);
}
/* Inbound TP.CM RTS/BAM: size in range AND the packet count matches it exactly. */
static inline bool tp_cm_ok(uint16_t size, uint16_t total_packets)
{
    return tp_len_ok(size) && (total_packets == packets_for(size));
}
static inline void bit_set(uint8_t *bm, uint16_t seq) { bm[(seq - 1) >> 3] |= (uint8_t)(1u << ((seq - 1) & 7u)); }
static inline bool bit_get(const uint8_t *bm, uint16_t seq) { return (bm[(seq - 1) >> 3] >> ((seq - 1) & 7u)) & 1u; }

/* Emit one J1939 frame through the injected sink. */
static esp_err_t emit(struct e32p_j1939tp_ctx *c, uint8_t prio, uint32_t pgn,
                      uint8_t dst, const uint8_t *data, uint8_t len)
{
    e32p_j1939_message_t m = { .priority = prio, .pgn = pgn, .src_addr = c->src_addr,
                               .dst_addr = dst, .len = len };
    memcpy(m.data, data, len);
    return c->tx(&m, c->user);
}

static esp_err_t emit_cm(struct e32p_j1939tp_ctx *c, uint8_t prio, uint8_t dst,
                         const uint8_t cm[8])
{
    return emit(c, prio, E32P_J1939_PGN_TP_CM, dst, cm, 8);
}

/* Build and emit TP.DT packet @p seq from a message buffer. */
static esp_err_t emit_dt(struct e32p_j1939tp_ctx *c, uint8_t prio, uint8_t dst,
                         const uint8_t *buf, uint16_t len, uint16_t seq)
{
    uint8_t d[8];
    d[0] = (uint8_t)seq;
    const uint16_t off = (uint16_t)((seq - 1) * TP_DT_PAYLOAD);
    for (uint8_t i = 0; i < TP_DT_PAYLOAD; i++) {
        d[1 + i] = (off + i < len) ? buf[off + i] : 0xFFu;
    }
    return emit(c, prio, E32P_J1939_PGN_TP_DT, dst, d, 8);
}

static void send_abort(struct e32p_j1939tp_ctx *c, uint8_t dst, uint32_t pgn, uint8_t reason)
{
    uint8_t cm[8] = { TP_CTRL_ABORT, reason, 0xFF, 0xFF, 0xFF, 0, 0, 0 };
    put_pgn(&cm[5], pgn);
    (void)emit_cm(c, TP_PRIO_DEFAULT, dst, cm);
}

static void tx_finish(struct e32p_j1939tp_ctx *c, esp_err_t status)
{
    tx_session_t *t = &c->txs;
    const uint32_t pgn = t->pgn;
    const uint8_t dst = t->dst;
    t->state = TX_IDLE;
    if (c->on_tx_done) {
        c->on_tx_done(pgn, dst, status, c->user);
    }
}

/* --- RX session management ------------------------------------------------ */

static rx_session_t *rx_find(struct e32p_j1939tp_ctx *c, uint8_t src)
{
    for (int i = 0; i < TP_MAX_RX; i++) {
        if (c->rx[i].kind != RX_FREE && c->rx[i].src == src) {
            return &c->rx[i];
        }
    }
    return NULL;
}
static rx_session_t *rx_alloc(struct e32p_j1939tp_ctx *c)
{
    for (int i = 0; i < TP_MAX_RX; i++) {
        if (c->rx[i].kind == RX_FREE) {
            return &c->rx[i];
        }
    }
    return NULL;
}
static void rx_free(rx_session_t *r)
{
    r->kind = RX_FREE;
    memset(r->seen, 0, sizeof(r->seen));
    r->received = 0;
}

static void rx_open(struct e32p_j1939tp_ctx *c, rx_session_t *r, rx_kind_t kind,
                    const e32p_j1939_message_t *f, const uint8_t *cm, uint32_t now_ms)
{
    r->kind = kind;
    r->src = f->src_addr;
    r->dst = f->dst_addr;
    r->size = (uint16_t)(cm[1] | (cm[2] << 8));
    r->total_packets = cm[3];
    r->pgn = get_pgn(&cm[5]);
    r->received = 0;
    r->burst_end = 0;
    r->last_ms = now_ms;
    memset(r->seen, 0, sizeof(r->seen));
}

/* Send a CTS granting the next burst; updates burst_end. */
static void rx_send_cts(struct e32p_j1939tp_ctx *c, rx_session_t *r)
{
    const uint16_t remaining = (uint16_t)(r->total_packets - r->received);
    const uint16_t grant = (remaining < TP_CTS_BURST) ? remaining : TP_CTS_BURST;
    const uint16_t next = (uint16_t)(r->received + 1);
    uint8_t cm[8] = { TP_CTRL_CTS, (uint8_t)grant, (uint8_t)next, 0xFF, 0xFF, 0, 0, 0 };
    put_pgn(&cm[5], r->pgn);
    r->burst_end = (uint16_t)(r->received + grant);
    (void)emit_cm(c, TP_PRIO_DEFAULT, r->src, cm);
}

static void rx_complete(struct e32p_j1939tp_ctx *c, rx_session_t *r)
{
    if (r->kind == RX_CMDT) {
        uint8_t cm[8] = { TP_CTRL_EOMA, (uint8_t)(r->size & 0xFF), (uint8_t)(r->size >> 8),
                          (uint8_t)r->total_packets, 0xFF, 0, 0, 0 };
        put_pgn(&cm[5], r->pgn);
        (void)emit_cm(c, TP_PRIO_DEFAULT, r->src, cm);
    }
    if (c->on_rx) {
        c->on_rx(r->pgn, r->src, r->dst, r->buf, r->size, c->user);
    }
    rx_free(r);
}

/* --- Public API ----------------------------------------------------------- */

esp_err_t e32p_j1939tp_init(const e32p_j1939tp_config_t *cfg, e32p_j1939tp_handle_t *out)
{
    E32P_CHECK_ARG(cfg != NULL, TAG);
    E32P_CHECK_ARG(out != NULL, TAG);
    E32P_CHECK_ARG(cfg->tx != NULL, TAG);
    E32P_CHECK_ARG(cfg->on_rx != NULL, TAG);
    E32P_CHECK_ARG(cfg->src_addr <= 253u, TAG);

    struct e32p_j1939tp_ctx *c = calloc(1, sizeof(*c));
    ESP_RETURN_ON_FALSE(c != NULL, ESP_ERR_NO_MEM, TAG, "ctx alloc failed");

    c->txs.buf = malloc(TP_MAX_MSG);
    if (c->txs.buf == NULL) {
        free(c);
        return ESP_ERR_NO_MEM;
    }
    for (int i = 0; i < TP_MAX_RX; i++) {
        c->rx[i].buf = malloc(TP_MAX_MSG);
        if (c->rx[i].buf == NULL) {
            for (int j = 0; j < i; j++) free(c->rx[j].buf);
            free(c->txs.buf);
            free(c);
            return ESP_ERR_NO_MEM;
        }
    }
    c->src_addr = cfg->src_addr;
    c->tx = cfg->tx;
    c->on_rx = cfg->on_rx;
    c->on_tx_done = cfg->on_tx_done;
    c->user = cfg->user;

    *out = c;
    ESP_LOGI(TAG, "tp engine init: SA=0x%02X, %d rx sessions, %d byte max msg",
             c->src_addr, TP_MAX_RX, TP_MAX_MSG);
    return ESP_OK;
}

esp_err_t e32p_j1939tp_deinit(e32p_j1939tp_handle_t h)
{
    if (h == NULL) {
        return ESP_OK;
    }
    for (int i = 0; i < TP_MAX_RX; i++) {
        free(h->rx[i].buf);
    }
    free(h->txs.buf);
    free(h);
    return ESP_OK;
}

bool e32p_j1939tp_tx_busy(e32p_j1939tp_handle_t h)
{
    return (h != NULL) && (h->txs.state != TX_IDLE);
}

esp_err_t e32p_j1939tp_send(e32p_j1939tp_handle_t h, uint32_t pgn, uint8_t priority,
                            uint8_t dst, const uint8_t *data, uint16_t len)
{
    E32P_CHECK_ARG(h != NULL, TAG);
    E32P_CHECK_ARG(data != NULL, TAG);
    E32P_CHECK_ARG(tp_len_ok(len), TAG);
    ESP_RETURN_ON_FALSE(h->txs.state == TX_IDLE, ESP_ERR_INVALID_STATE, TAG, "tx busy");

    tx_session_t *t = &h->txs;
    memcpy(t->buf, data, len);
    t->pgn = pgn;
    t->prio = (priority <= 7u) ? priority : TP_PRIO_DEFAULT;
    t->dst = dst;
    t->len = len;
    t->total_packets = packets_for(len);
    t->next_seq = 1;
    t->burst_left = 0;
    t->timer_set = false;

    uint8_t cm[8];
    put_pgn(&cm[5], pgn);
    cm[1] = (uint8_t)(len & 0xFFu);
    cm[2] = (uint8_t)(len >> 8);
    cm[3] = (uint8_t)t->total_packets;

    if (dst == E32P_J1939_ADDR_GLOBAL) {           /* BAM */
        cm[0] = TP_CTRL_BAM;
        cm[4] = 0xFF;
        t->state = TX_BAM;
        t->bam_next_ms = 0;                        /* first poll emits packet 1 */
        return emit_cm(h, t->prio, E32P_J1939_ADDR_GLOBAL, cm);
    }
    cm[0] = TP_CTRL_RTS;                            /* connection mode */
    cm[4] = 0xFF;                                   /* max packets/CTS: no limit */
    t->state = TX_RTS_WAIT;
    return emit_cm(h, t->prio, dst, cm);
}

/* Route an inbound TP.DT into its RX session. */
static void on_dt(struct e32p_j1939tp_ctx *c, const e32p_j1939_message_t *f, uint32_t now_ms)
{
    rx_session_t *r = rx_find(c, f->src_addr);
    if (r == NULL) {
        return;  /* no open session for this source */
    }
    const uint16_t seq = f->data[0];
    if (seq < 1 || seq > r->total_packets) {
        return;
    }
    const uint16_t off = (uint16_t)((seq - 1) * TP_DT_PAYLOAD);
    if (off >= r->size) {
        return;  /* defence in depth: tp_cm_ok() already rules this out */
    }
    r->last_ms = now_ms;
    if (!bit_get(r->seen, seq)) {
        bit_set(r->seen, seq);
        const uint16_t n = (uint16_t)((off + TP_DT_PAYLOAD <= r->size)
                                          ? TP_DT_PAYLOAD : (r->size - off));
        memcpy(&r->buf[off], &f->data[1], n);
        r->received++;
    }
    if (r->received >= r->total_packets) {
        rx_complete(c, r);                 /* delivers + (CMDT) EndOfMsgAck */
    } else if (r->kind == RX_CMDT && r->received >= r->burst_end) {
        rx_send_cts(c, r);                 /* grant the next burst */
    }
}

esp_err_t e32p_j1939tp_on_frame(e32p_j1939tp_handle_t h, const e32p_j1939_message_t *frame)
{
    E32P_CHECK_ARG(h != NULL, TAG);
    E32P_CHECK_ARG(frame != NULL, TAG);

    if (frame->pgn == E32P_J1939_PGN_TP_DT) {
        on_dt(h, frame, h->now_ms);
        return ESP_OK;
    }
    if (frame->pgn != E32P_J1939_PGN_TP_CM) {
        return ESP_OK;  /* not a transport frame */
    }

    const uint8_t *cm = frame->data;
    tx_session_t *t = &h->txs;

    switch (cm[0]) {
    case TP_CTRL_BAM: {
        rx_session_t *r = rx_find(h, frame->src_addr);
        if (r == NULL) r = rx_alloc(h);
        if (r == NULL) return ESP_OK;               /* no free session: drop */
        rx_open(h, r, RX_BAM, frame, cm, h->now_ms);
        if (!tp_cm_ok(r->size, r->total_packets)) rx_free(r);   /* BAM: drop silently */
        break;
    }
    case TP_CTRL_RTS: {
        if (frame->dst_addr != h->src_addr) break;  /* not for us */
        rx_session_t *r = rx_find(h, frame->src_addr);
        if (r != NULL) { send_abort(h, frame->src_addr, get_pgn(&cm[5]), TP_ABORT_BUSY); break; }
        r = rx_alloc(h);
        if (r == NULL) { send_abort(h, frame->src_addr, get_pgn(&cm[5]), TP_ABORT_RESOURCES); break; }
        rx_open(h, r, RX_CMDT, frame, cm, h->now_ms);
        if (!tp_cm_ok(r->size, r->total_packets)) {
            send_abort(h, frame->src_addr, r->pgn, TP_ABORT_RESOURCES);
            rx_free(r);
            break;
        }
        rx_send_cts(h, r);                           /* grant first burst */
        break;
    }
    case TP_CTRL_CTS: {
        if (t->state != TX_RTS_WAIT && t->state != TX_CMDT_WAIT) break;
        if (frame->src_addr != t->dst) break;
        const uint8_t num = cm[1];
        const uint8_t next = cm[2];
        if (num == 0) { t->timer_set = false; break; } /* hold / wait */
        t->next_seq = next;
        t->burst_left = num;
        t->state = TX_CMDT_SEND;
        break;
    }
    case TP_CTRL_EOMA:
        if (t->state == TX_CMDT_WAIT && frame->src_addr == t->dst) {
            tx_finish(h, ESP_OK);
        }
        break;
    case TP_CTRL_ABORT:
        if (t->state != TX_IDLE && frame->src_addr == t->dst) {
            tx_finish(h, ESP_FAIL);
        } else {
            rx_session_t *r = rx_find(h, frame->src_addr);
            if (r != NULL) rx_free(r);
        }
        break;
    default:
        break;
    }
    return ESP_OK;
}

void e32p_j1939tp_poll(e32p_j1939tp_handle_t h, uint32_t now_ms)
{
    if (h == NULL) {
        return;
    }
    h->now_ms = now_ms;
    tx_session_t *t = &h->txs;

    switch (t->state) {
    case TX_BAM:
        if (now_ms >= t->bam_next_ms) {
            (void)emit_dt(h, t->prio, E32P_J1939_ADDR_GLOBAL, t->buf, t->len, t->next_seq);
            t->next_seq++;
            if (t->next_seq > t->total_packets) {
                tx_finish(h, ESP_OK);
            } else {
                t->bam_next_ms = now_ms + TP_BAM_PACE_MS;
            }
        }
        break;
    case TX_RTS_WAIT:
    case TX_CMDT_WAIT:
        if (!t->timer_set) { t->timer_ms = now_ms; t->timer_set = true; }
        else if (now_ms - t->timer_ms > TP_T3_MS) {
            send_abort(h, t->dst, t->pgn, TP_ABORT_TIMEOUT);
            tx_finish(h, ESP_ERR_TIMEOUT);
        }
        break;
    case TX_CMDT_SEND:
        while (t->burst_left > 0) {
            (void)emit_dt(h, t->prio, t->dst, t->buf, t->len, t->next_seq);
            t->next_seq++;
            t->burst_left--;
        }
        t->state = TX_CMDT_WAIT;
        t->timer_set = false;
        break;
    default:
        break;
    }

    /* RX inter-packet timeouts. */
    for (int i = 0; i < TP_MAX_RX; i++) {
        rx_session_t *r = &h->rx[i];
        if (r->kind == RX_FREE) {
            continue;
        }
        if (now_ms - r->last_ms > TP_T1_MS) {
            if (r->kind == RX_CMDT) {
                send_abort(h, r->src, r->pgn, TP_ABORT_TIMEOUT);
            }
            ESP_LOGW(TAG, "rx session from 0x%02X timed out (%u/%u pkts)",
                     r->src, r->received, r->total_packets);
            rx_free(r);
        }
    }
}
