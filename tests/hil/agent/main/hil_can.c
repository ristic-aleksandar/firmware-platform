/**
 * @file hil_can.c
 * @brief CAN / J1939 HIL tests (Phase 1) + the autonomous COMPANION peer.
 */
#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "driver/twai.h"   /* raw status/recovery for the companion's bus-off handling */
#include "e32p_twai.h"
#include "e32p_j1939.h"
#include "e32p_j1939tp.h"
#include "e32p_j1939_acl.h"
#include "hil_agent.h"
#include "hil_can.h"
#include "hil_diag.h"
#include "e32p_time.h"

#include "hil_pins.h"
#define CAN_TX_GPIO HIL_TWAI_TX
#define CAN_RX_GPIO HIL_TWAI_RX
#define CAN_BITRATE HIL_CAN_BITRATE   /* bus-wide rate - see hil_pins.h */
#define DUT_SA      0x80u
#define COMP_SA     0x21u
#define DEMO_PGN    65262u   /* 0xFEEE, PDU2 broadcast */

/* Companion receive block. 5 ms lets the diagnostics peer honour a 1 ms STmin
   while the loop still blocks rather than spins (ticket D-01). */
#define COMP_RX_TIMEOUT_MS 5u
E32P_ASSERT_BLOCKING_MS(COMP_RX_TIMEOUT_MS);

/* The companion routes 29-bit frames with PF 0xDA/0xDB to diagnostics (see
   hil_diag_is_isotp_frame). No J1939 PGN this file uses may land there. */
#define HIL_PF(pgn) (((pgn) >> 8) & 0xFFu)
_Static_assert(HIL_PF(DEMO_PGN) != 0xDAu && HIL_PF(DEMO_PGN) != 0xDBu &&
               HIL_PF(E32P_J1939_PGN_TP_CM) != 0xDAu && HIL_PF(E32P_J1939_PGN_TP_CM) != 0xDBu &&
               HIL_PF(E32P_J1939_PGN_TP_DT) != 0xDAu && HIL_PF(E32P_J1939_PGN_TP_DT) != 0xDBu,
               "a J1939 PGN collides with ISO-TP normal-fixed PF 0xDA/0xDB");

/* ===================== Single-board tests (no bus) ====================== */

hil_verdict_t hil_can_twai_neg(char *d, size_t n)
{
    e32p_twai_deinit();  /* clear any prior state */
    e32p_twai_config_t bad = { .tx_gpio = CAN_TX_GPIO, .rx_gpio = CAN_RX_GPIO,
                               .bitrate_bps = 999, .mode = E32P_TWAI_MODE_NO_ACK };
    if (e32p_twai_init(&bad) != ESP_ERR_INVALID_ARG) {
        snprintf(d, n, "bad bitrate not rejected");
        return HIL_FAIL;
    }
    if (e32p_twai_init(NULL) != ESP_ERR_INVALID_ARG) {
        snprintf(d, n, "NULL cfg not rejected");
        return HIL_FAIL;
    }
    snprintf(d, n, "bad args rejected");
    return HIL_PASS;
}

hil_verdict_t hil_can_twai_selftx(char *d, size_t n)
{
    e32p_twai_deinit();
    e32p_twai_config_t cfg = { .tx_gpio = CAN_TX_GPIO, .rx_gpio = CAN_RX_GPIO,
                               .bitrate_bps = CAN_BITRATE, .mode = E32P_TWAI_MODE_NO_ACK };
    if (e32p_twai_init(&cfg) != ESP_OK) {
        snprintf(d, n, "init failed");
        return HIL_FAIL;
    }
    e32p_twai_start();
    e32p_twai_message_t m = { .id = 0x18FEEE80, .extended = true, .self = true,
                              .len = 4, .data = { 1, 2, 3, 4 } };
    esp_err_t err = e32p_twai_transmit(&m, 100);
    e32p_twai_stop();
    e32p_twai_deinit();
    if (err != ESP_OK) {
        snprintf(d, n, "transmit=%s", esp_err_to_name(err));
        return HIL_FAIL;
    }
    snprintf(d, n, "queued 1 frame (NO_ACK)");
    return HIL_PASS;
}

hil_verdict_t hil_can_j1939_codec(char *d, size_t n)
{
    /* EEC1: PGN 61444 (0xF004, PDU2), prio 3, SA 0 -> classic ID 0x0CF00400. */
    uint32_t id = e32p_j1939_id_encode(3, 61444u, 0x00u, E32P_J1939_ADDR_GLOBAL);
    if (id != 0x0CF00400u) {
        snprintf(d, n, "EEC1 id=0x%08lX", (unsigned long)id);
        return HIL_FAIL;
    }
    uint8_t prio, src, dst;
    uint32_t pgn;
    e32p_j1939_id_decode(id, &prio, &pgn, &src, &dst);
    if (pgn != 61444u || prio != 3 || src != 0 || dst != E32P_J1939_ADDR_GLOBAL) {
        snprintf(d, n, "decode mismatch");
        return HIL_FAIL;
    }
    /* PDU1: TP.CM 60416, prio 7, SA 0x80, DA 0x21 -> 0x1CEC2180. */
    if (e32p_j1939_id_encode(7, E32P_J1939_PGN_TP_CM, 0x80u, 0x21u) != 0x1CEC2180u) {
        snprintf(d, n, "PDU1 encode mismatch");
        return HIL_FAIL;
    }
    if (!e32p_j1939_pgn_is_pdu1(0xEF00u) || e32p_j1939_pgn_is_pdu1(0xF000u)) {
        snprintf(d, n, "PF=240 boundary wrong");
        return HIL_FAIL;
    }
    snprintf(d, n, "codec ok (EEC1=0x%08lX)", (unsigned long)id);
    return HIL_PASS;
}

hil_verdict_t hil_can_j1939_neg(char *d, size_t n)
{
    e32p_j1939_config_t bad = { .src_addr = 254u };  /* out of 0..253 */
    if (e32p_j1939_init(&bad) != ESP_ERR_INVALID_ARG) {
        snprintf(d, n, "SA>253 not rejected");
        return HIL_FAIL;
    }
    if (e32p_j1939_init(NULL) != ESP_ERR_INVALID_ARG) {
        snprintf(d, n, "NULL cfg not rejected");
        return HIL_FAIL;
    }
    e32p_j1939_deinit();
    e32p_j1939_message_t m = { .pgn = DEMO_PGN, .len = 0 };
    if (e32p_j1939_send(&m, 10) != ESP_ERR_INVALID_STATE) {
        snprintf(d, n, "send-before-init not rejected");
        return HIL_FAIL;
    }
    snprintf(d, n, "bad args rejected");
    return HIL_PASS;
}

/* --- in-process TP loopback (two engines, no bus) --- */

static struct { e32p_j1939_message_t f; int to; } s_q[128];
static int s_qh, s_qt;
static e32p_j1939tp_handle_t s_la, s_lb;
static bool s_lrxdone;
static uint16_t s_lrxlen;
static uint8_t s_lrxbuf[300];

static esp_err_t loop_tx_a(const e32p_j1939_message_t *m, void *u)
{
    (void)u; s_q[s_qt].f = *m; s_q[s_qt].to = 1; s_qt = (s_qt + 1) & 127; return ESP_OK;
}
static esp_err_t loop_tx_b(const e32p_j1939_message_t *m, void *u)
{
    (void)u; s_q[s_qt].f = *m; s_q[s_qt].to = 0; s_qt = (s_qt + 1) & 127; return ESP_OK;
}
static void loop_rx(uint32_t pgn, uint8_t src, uint8_t dst,
                    const uint8_t *data, uint16_t len, void *u)
{
    (void)pgn; (void)src; (void)dst; (void)u;
    if (len <= sizeof(s_lrxbuf)) {
        memcpy(s_lrxbuf, data, len);
        s_lrxlen = len;
        s_lrxdone = true;
    }
}
static void loop_pump(void)
{
    while (s_qh != s_qt) {
        e32p_j1939_message_t f = s_q[s_qh].f;
        int to = s_q[s_qh].to;
        s_qh = (s_qh + 1) & 127;
        e32p_j1939tp_on_frame(to == 0 ? s_la : s_lb, &f);
    }
}

hil_verdict_t hil_can_tp_loop(char *d, size_t n)
{
    s_qh = s_qt = 0;
    s_lrxdone = false;
    s_lrxlen = 0;
    e32p_j1939tp_config_t ca = { .src_addr = DUT_SA, .tx = loop_tx_a, .on_rx = loop_rx };
    e32p_j1939tp_config_t cb = { .src_addr = COMP_SA, .tx = loop_tx_b, .on_rx = loop_rx };
    if (e32p_j1939tp_init(&ca, &s_la) != ESP_OK) {
        snprintf(d, n, "engine A init failed");
        return HIL_FAIL;
    }
    if (e32p_j1939tp_init(&cb, &s_lb) != ESP_OK) {
        e32p_j1939tp_deinit(s_la);
        snprintf(d, n, "engine B init failed");
        return HIL_FAIL;
    }
    uint8_t msg[100];
    for (int i = 0; i < 100; i++) {
        msg[i] = (uint8_t)(i * 3 + 1);
    }
    esp_err_t err = e32p_j1939tp_send(s_la, DEMO_PGN, 6, E32P_J1939_ADDR_GLOBAL, msg, 100);
    if (err != ESP_OK) {
        e32p_j1939tp_deinit(s_la);
        e32p_j1939tp_deinit(s_lb);
        snprintf(d, n, "send=%s", esp_err_to_name(err));
        return HIL_FAIL;
    }
    for (uint32_t t = 0; t < 6000 && !s_lrxdone; t += 25) {
        e32p_j1939tp_poll(s_la, t); loop_pump();
        e32p_j1939tp_poll(s_lb, t); loop_pump();
    }
    bool ok = s_lrxdone && s_lrxlen == 100 && memcmp(msg, s_lrxbuf, 100) == 0;
    e32p_j1939tp_deinit(s_la);
    e32p_j1939tp_deinit(s_lb);
    if (!ok) {
        snprintf(d, n, "reassembly failed (len=%u)", s_lrxlen);
        return HIL_FAIL;
    }
    snprintf(d, n, "BAM 100B reassembled in-process");
    return HIL_PASS;
}

static esp_err_t null_tx(const e32p_j1939_message_t *m, void *u) { (void)m; (void)u; return ESP_OK; }
static void null_rx(uint32_t p, uint8_t s, uint8_t ds, const uint8_t *dt, uint16_t l, void *u)
{ (void)p; (void)s; (void)ds; (void)dt; (void)l; (void)u; }

hil_verdict_t hil_can_tp_neg(char *d, size_t n)
{
    e32p_j1939tp_handle_t h = NULL;
    e32p_j1939tp_config_t bad = { .src_addr = DUT_SA, .tx = NULL, .on_rx = null_rx };
    if (e32p_j1939tp_init(&bad, &h) != ESP_ERR_INVALID_ARG) {
        snprintf(d, n, "NULL tx not rejected");
        return HIL_FAIL;
    }
    e32p_j1939tp_config_t good = { .src_addr = DUT_SA, .tx = null_tx, .on_rx = null_rx };
    if (e32p_j1939tp_init(&good, &h) != ESP_OK) {
        snprintf(d, n, "init failed");
        return HIL_FAIL;
    }
    uint8_t small[4] = { 1, 2, 3, 4 };
    hil_verdict_t v = HIL_PASS;
    if (e32p_j1939tp_send(h, DEMO_PGN, 6, COMP_SA, small, 4) != ESP_ERR_INVALID_ARG) {
        snprintf(d, n, "len<9 not rejected"); v = HIL_FAIL;
    } else if (e32p_j1939tp_send(h, DEMO_PGN, 6, COMP_SA, NULL, 100) != ESP_ERR_INVALID_ARG) {
        snprintf(d, n, "NULL data not rejected"); v = HIL_FAIL;
    } else {
        snprintf(d, n, "bad args rejected");
    }
    e32p_j1939tp_deinit(h);
    return v;
}

/* ================= Two-board tests (need companion + xcvr) ============== */

static esp_err_t bus_up_normal(void)
{
    e32p_twai_deinit();
    e32p_twai_config_t cfg = { .tx_gpio = CAN_TX_GPIO, .rx_gpio = CAN_RX_GPIO,
                               .bitrate_bps = CAN_BITRATE, .mode = E32P_TWAI_MODE_NORMAL };
    esp_err_t err = e32p_twai_init(&cfg);
    if (err != ESP_OK) {
        return err;
    }
    return e32p_twai_start();
}
static void bus_down(void)
{
    /* Let any queued frame actually reach the wire before stopping the
       controller. e32p_twai_transmit only QUEUES the frame (twai_transmit
       returns once it's enqueued, not sent), so a transmit followed by an
       immediate stop+deinit aborts the frame and the peer never sees it — which
       silently broke the single-frame and last-TP-packet cases. Wait for the TX
       queue to drain (bounded, in case there is no peer to ACK). */
    twai_status_info_t st;
    for (int i = 0; i < 60; i++) {   /* up to ~300 ms */
        if (twai_get_status_info(&st) != ESP_OK || st.msgs_to_tx == 0) {
            break;
        }
        E32P_ASSERT_BLOCKING_MS(5);    /* 60 x 5 ms = the ~300 ms budget above */
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    e32p_twai_stop();
    e32p_twai_deinit();
}

/* Shared with hil_diag.c. Thin prefixed wrappers, so the existing static
   helpers and all their call sites stay exactly as they were. */
esp_err_t hil_can_bus_up_normal(void)
{
    return bus_up_normal();
}

void hil_can_bus_down(void)
{
    bus_down();
}

/* How long can_twai_xfer waits for the driver to report the queued frame done. */
#define XFER_TX_WAIT_MS 200u
#define XFER_ALERTS (TWAI_ALERT_TX_SUCCESS | TWAI_ALERT_TX_FAILED | TWAI_ALERT_BUS_ERROR | \
                     TWAI_ALERT_ERR_PASS | TWAI_ALERT_BUS_OFF)

hil_verdict_t hil_can_twai_xfer(char *d, size_t n)
{
    if (bus_up_normal() != ESP_OK) {
        snprintf(d, n, "bus up failed");
        return HIL_FAIL;
    }
    /* e32p_twai_transmit only QUEUES the frame, so a dead transceiver or a
       missing ACK still returned ESP_OK here. The driver's TX_SUCCESS alert is
       raised only when the controller finished the frame on the bus with an
       ACK (NORMAL mode), so PASS needs that alert. The driver starts with all
       alerts off and nothing else on the DUT uses them; the mask is set back to
       none before bus_down(). */
    (void)twai_reconfigure_alerts(XFER_ALERTS, NULL);
    e32p_twai_message_t m = { .id = 0x18FEEE80, .extended = true, .len = 8,
                              .data = { 0xAA, 1, 2, 3, 4, 5, 6, 7 } };
    esp_err_t err = e32p_twai_transmit(&m, 500);  /* NORMAL: needs a peer to ACK */
    uint32_t seen = 0;
    if (err == ESP_OK) {
        /* Bus errors repeat while the controller retries, so keep reading until
           TX success, TX failed, bus-off or the deadline. */
        const int64_t deadline_us = esp_timer_get_time() + (int64_t)XFER_TX_WAIT_MS * 1000;
        int64_t left_us;
        while (!(seen & (TWAI_ALERT_TX_SUCCESS | TWAI_ALERT_TX_FAILED | TWAI_ALERT_BUS_OFF)) &&
               (left_us = deadline_us - esp_timer_get_time()) > 0) {
            uint32_t a = 0;
            if (twai_read_alerts(&a, E32P_MS_TO_TICKS_ATLEAST1((uint32_t)(left_us / 1000))) == ESP_OK) {
                seen |= a;
            }
        }
    }
    twai_status_info_t st = { 0 };
    (void)twai_get_status_info(&st);
    (void)twai_reconfigure_alerts(TWAI_ALERT_NONE, NULL);
    bus_down();
    if (err != ESP_OK) {
        snprintf(d, n, "tx=%s (need companion+xcvr)", esp_err_to_name(err));
        return HIL_FAIL;
    }
    if (!(seen & TWAI_ALERT_TX_SUCCESS)) {
        snprintf(d, n, "queued but no TX success in %u ms (no ACK? dead xcvr?) state=%d tec=%lu alerts=0x%lx",
                 (unsigned)XFER_TX_WAIT_MS, (int)st.state, (unsigned long)st.tx_error_counter,
                 (unsigned long)seen);
        return HIL_FAIL;
    }
    snprintf(d, n, "frame transmitted+ACKed (TX success alert)");
    return HIL_PASS;
}

hil_verdict_t hil_can_j1939_single(char *d, size_t n)
{
    if (bus_up_normal() != ESP_OK) {
        snprintf(d, n, "bus up failed");
        return HIL_FAIL;
    }
    e32p_j1939_config_t node = { .src_addr = DUT_SA, .default_priority = 6, .self_reception = false };
    e32p_j1939_init(&node);
    e32p_j1939_message_t m = { .priority = 0xFF, .pgn = DEMO_PGN,
                               .dst_addr = E32P_J1939_ADDR_GLOBAL, .len = 8,
                               .data = { 0xBB, 1, 2, 3, 4, 5, 6, 7 } };
    esp_err_t err = e32p_j1939_send(&m, 500);
    e32p_j1939_deinit();
    bus_down();
    if (err != ESP_OK) {
        snprintf(d, n, "send=%s (need companion+xcvr)", esp_err_to_name(err));
        return HIL_FAIL;
    }
    snprintf(d, n, "PGN %u single-frame sent", (unsigned)DEMO_PGN);
    return HIL_PASS;
}

hil_verdict_t hil_can_j1939_rx(char *d, size_t n)
{
    if (bus_up_normal() != ESP_OK) {
        snprintf(d, n, "bus up failed");
        return HIL_FAIL;
    }
    e32p_j1939_config_t node = { .src_addr = DUT_SA, .default_priority = 6, .self_reception = false };
    e32p_j1939_init(&node);
    /* The companion broadcasts PGN 65262 only in response to a frame (it stays
       silent on an idle bus so it never error-counts to bus-off). Poke it with a
       trigger, then listen for its reply; retry so we don't depend on a single
       frame getting through. */
    e32p_j1939_message_t trig = { .priority = 6, .pgn = DEMO_PGN,
                                  .dst_addr = E32P_J1939_ADDR_GLOBAL, .len = 1, .data = { 0 } };
    e32p_j1939_message_t rx = {0};
    esp_err_t err = ESP_ERR_TIMEOUT;
    for (int i = 0; i < 6; i++) {
        e32p_j1939_send(&trig, 100);
        if (e32p_j1939_receive(&rx, 400) == ESP_OK) {
            err = ESP_OK;
            break;
        }
    }
    e32p_j1939_deinit();
    bus_down();
    if (err != ESP_OK) {
        snprintf(d, n, "rx=%s (need companion+xcvr)", esp_err_to_name(err));
        return HIL_FAIL;
    }
    snprintf(d, n, "rx pgn=%lu sa=0x%02X len=%u",
             (unsigned long)rx.pgn, rx.src_addr, rx.len);
    return HIL_PASS;
}

/* TP transfer over the real bus (BAM if dst==global, else RTS/CTS). */
static volatile bool s_dut_txdone;
static volatile esp_err_t s_dut_txstatus;
static esp_err_t dut_tp_tx(const e32p_j1939_message_t *m, void *u) { (void)u; return e32p_j1939_send(m, 50); }
static void dut_tp_rx(uint32_t p, uint8_t s, uint8_t ds, const uint8_t *dt, uint16_t l, void *u)
{ (void)p; (void)s; (void)ds; (void)dt; (void)l; (void)u; }
static void dut_tp_done(uint32_t p, uint8_t dst, esp_err_t st, void *u)
{ (void)p; (void)dst; (void)u; s_dut_txstatus = st; s_dut_txdone = true; }

static uint8_t s_tp_buf[1785];

static hil_verdict_t tp_send_over_bus(uint8_t dst, uint16_t len, char *d, size_t n)
{
    if (bus_up_normal() != ESP_OK) {
        snprintf(d, n, "bus up failed");
        return HIL_FAIL;
    }
    e32p_j1939_config_t node = { .src_addr = DUT_SA, .default_priority = 6, .self_reception = false };
    e32p_j1939_init(&node);
    e32p_j1939tp_config_t tc = { .src_addr = DUT_SA, .tx = dut_tp_tx,
                                 .on_rx = dut_tp_rx, .on_tx_done = dut_tp_done };
    e32p_j1939tp_handle_t h = NULL;
    if (e32p_j1939tp_init(&tc, &h) != ESP_OK) {
        e32p_j1939_deinit();
        bus_down();
        snprintf(d, n, "tp init failed");
        return HIL_FAIL;
    }
    for (uint16_t i = 0; i < len; i++) {
        s_tp_buf[i] = (uint8_t)(i + 1);
    }
    s_dut_txdone = false;
    s_dut_txstatus = ESP_FAIL;
    esp_err_t err = e32p_j1939tp_send(h, DEMO_PGN, 6, dst, s_tp_buf, len);
    if (err != ESP_OK) {
        e32p_j1939tp_deinit(h);
        e32p_j1939_deinit();
        bus_down();
        snprintf(d, n, "tp send=%s", esp_err_to_name(err));
        return HIL_FAIL;
    }
    uint32_t base = (uint32_t)(esp_timer_get_time() / 1000);
    while (!s_dut_txdone &&
           ((uint32_t)(esp_timer_get_time() / 1000) - base) < 6000) {
        uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
        e32p_j1939tp_poll(h, now);
        e32p_j1939_message_t m;
        E32P_ASSERT_BLOCKING_MS(5);    /* a real 5 ms block, not a spin (D-01) */
        if (e32p_j1939_receive(&m, 5) == ESP_OK) {
            e32p_j1939tp_on_frame(h, &m);
        }
    }
    bool ok = s_dut_txdone && s_dut_txstatus == ESP_OK;
    e32p_j1939tp_deinit(h);
    e32p_j1939_deinit();
    bus_down();
    if (!ok) {
        snprintf(d, n, "tp %uB %s (need companion+xcvr)", (unsigned)len,
                 s_dut_txdone ? esp_err_to_name(s_dut_txstatus) : "timeout");
        return HIL_FAIL;
    }
    snprintf(d, n, "tp %uB delivered", (unsigned)len);
    return HIL_PASS;
}

hil_verdict_t hil_can_tp_bam(char *d, size_t n)
{
    return tp_send_over_bus(E32P_J1939_ADDR_GLOBAL, 250, d, n);
}
hil_verdict_t hil_can_tp_cmdt(char *d, size_t n)
{
    return tp_send_over_bus(COMP_SA, 250, d, n);
}

#define STRESS_TX_TIMEOUT_MS 100   /* per-frame wait for a free TX queue slot */
#define STRESS_MAX_STALLS    5     /* consecutive timeouts that mean a dead bus */

hil_verdict_t hil_can_stress(char *d, size_t n)
{
    if (bus_up_normal() != ESP_OK) {
        snprintf(d, n, "bus up failed");
        return HIL_FAIL;
    }
    uint32_t heap0 = esp_get_free_heap_size();
    const int total = 1000;
    int sent = 0;
    int stalled = 0;   /* consecutive transmits that timed out */
    for (int i = 0; i < total; i++) {
        e32p_twai_message_t m = { .id = 0x18FEEE80, .extended = true, .len = 8,
                                  .data = { (uint8_t)i, 1, 2, 3, 4, 5, 6, 7 } };
        if (e32p_twai_transmit(&m, STRESS_TX_TIMEOUT_MS) == ESP_OK) {
            sent++;
            stalled = 0;
        } else if (++stalled >= STRESS_MAX_STALLS) {
            /* Nothing is draining the TX queue - no node can acknowledge. Each
               further attempt would only wait out its own timeout: ~94 s to
               reach the same verdict. A healthy bus empties a 64-deep queue in
               ~35 ms, so 5 x 100 ms in a row is already far past a hiccup. */
            break;
        }
    }
    int32_t dheap = (int32_t)(esp_get_free_heap_size() - heap0);
    bus_down();
    if (sent < total) {
        snprintf(d, n, "sent %d/%d%s (need companion+xcvr)", sent, total,
                 (stalled >= STRESS_MAX_STALLS) ? ", gave up: TX queue not draining" : "");
        return HIL_FAIL;
    }
    if (dheap < -256 || dheap > 256) {
        snprintf(d, n, "heap drift %+d over %d frames", (int)dheap, total);
        return HIL_FAIL;
    }
    snprintf(d, n, "%d/%d frames, heap %+d", sent, total, (int)dheap);
    return HIL_PASS;
}

/* ============ Address claiming (in-process two-engine contention) ======= */

static struct { e32p_j1939_message_t f; int to; } s_aclq[64];
static int s_aqh, s_aqt;
static e32p_j1939_acl_handle_t s_acl_a, s_acl_b;

static esp_err_t acl_tx_a(const e32p_j1939_message_t *m, void *u)
{ (void)u; s_aclq[s_aqt].f = *m; s_aclq[s_aqt].to = 1; s_aqt = (s_aqt + 1) & 63; return ESP_OK; }
static esp_err_t acl_tx_b(const e32p_j1939_message_t *m, void *u)
{ (void)u; s_aclq[s_aqt].f = *m; s_aclq[s_aqt].to = 0; s_aqt = (s_aqt + 1) & 63; return ESP_OK; }
static void acl_pump(void)
{
    while (s_aqh != s_aqt) {
        e32p_j1939_message_t f = s_aclq[s_aqh].f;
        int to = s_aclq[s_aqh].to;
        s_aqh = (s_aqh + 1) & 63;
        e32p_j1939_acl_on_frame(to == 0 ? s_acl_a : s_acl_b, &f);
    }
}
static uint64_t acl_name(uint32_t identity, bool arb)
{
    e32p_j1939_name_t n = { .arbitrary_address_capable = arb, .manufacturer_code = 100,
                            .identity_number = identity };
    return e32p_j1939_name_encode(&n);
}

hil_verdict_t hil_can_acl_contend(char *d, size_t nn)
{
    s_aqh = s_aqt = 0;
    e32p_j1939_acl_config_t ca = { .name = acl_name(100,  true), .preferred_sa = 0x80, .tx = acl_tx_a };
    e32p_j1939_acl_config_t cb = { .name = acl_name(9000, true), .preferred_sa = 0x80, .tx = acl_tx_b };
    if (e32p_j1939_acl_init(&ca, &s_acl_a) != ESP_OK) {
        snprintf(d, nn, "engine A init failed");
        return HIL_FAIL;
    }
    if (e32p_j1939_acl_init(&cb, &s_acl_b) != ESP_OK) {
        e32p_j1939_acl_deinit(s_acl_a);
        snprintf(d, nn, "engine B init failed");
        return HIL_FAIL;
    }
    e32p_j1939_acl_start(s_acl_a);
    e32p_j1939_acl_start(s_acl_b);
    for (uint32_t t = 0; t < 2000; t += 25) {
        e32p_j1939_acl_poll(s_acl_a, t);
        e32p_j1939_acl_poll(s_acl_b, t);
        acl_pump();
    }
    uint8_t sa_a = e32p_j1939_acl_current_sa(s_acl_a);
    uint8_t sa_b = e32p_j1939_acl_current_sa(s_acl_b);
    bool ok_a = e32p_j1939_acl_is_claimed(s_acl_a) && sa_a == 0x80;
    bool ok_b = e32p_j1939_acl_is_claimed(s_acl_b) && sa_b != 0x80;
    e32p_j1939_acl_deinit(s_acl_a);
    e32p_j1939_acl_deinit(s_acl_b);
    s_acl_a = s_acl_b = NULL;
    if (!ok_a || !ok_b) {
        snprintf(d, nn, "A sa=0x%02X ok=%d, B sa=0x%02X ok=%d", sa_a, ok_a, sa_b, ok_b);
        return HIL_FAIL;
    }
    snprintf(d, nn, "lower NAME won 0x%02X, higher relocated to 0x%02X", sa_a, sa_b);
    return HIL_PASS;
}

/* ===================== Autonomous COMPANION peer ======================= */

static e32p_j1939tp_handle_t s_comp_tp;

static esp_err_t comp_tp_tx(const e32p_j1939_message_t *m, void *u) { (void)u; return e32p_j1939_send(m, 50); }
static void comp_tp_rx(uint32_t pgn, uint8_t src, uint8_t dst,
                       const uint8_t *data, uint16_t len, void *u)
{
    (void)dst; (void)data; (void)u;
    hil_emit("@EVT TP pgn=%lu sa=0x%02X len=%u", (unsigned long)pgn, src, len);
}

/* Safety net: if the controller ever reaches BUS_OFF, recover and rejoin the bus
   (otherwise e32p_twai stays off it forever). With the response-only broadcast
   policy the companion no longer transmits into an idle bus, so its error
   counters stay near zero and this should essentially never fire — but a real
   CAN node must handle bus-off, so we keep it. */
static void comp_can_keepalive(void)
{
    twai_status_info_t st;
    if (twai_get_status_info(&st) != ESP_OK) {
        return;
    }
    /* Called every loop pass, and never blocks. Recovery is a state machine in
       the driver: BUS_OFF --initiate--> RECOVERING --(ISR, after 128 x 11
       recessive bits)--> STOPPED --twai_start--> RUNNING. The previous version
       waited up to 500 ms for STOPPED and then called twai_start() once. If
       recovery took longer, that start failed in RECOVERING and nothing retried,
       so the companion stayed off the bus until a board reset. Acting on each
       state as it is observed closes that gap, and the task no longer stops
       receiving while it waits. */
    switch (st.state) {
    case TWAI_STATE_BUS_OFF:
        twai_initiate_recovery();
        break;
    case TWAI_STATE_STOPPED:
        twai_start();
        break;
    default:            /* RUNNING, or RECOVERING: nothing to do yet */
        break;
    }
}

static void companion_task(void *arg)
{
    (void)arg;
    e32p_twai_config_t bus = { .tx_gpio = CAN_TX_GPIO, .rx_gpio = CAN_RX_GPIO,
                               .bitrate_bps = CAN_BITRATE, .mode = E32P_TWAI_MODE_NORMAL };
    if (e32p_twai_init(&bus) != ESP_OK) {
        hil_emit("@EVT CAN_ERR twai init failed");
        vTaskDelete(NULL);
        return;
    }
    e32p_twai_start();
    e32p_j1939_config_t node = { .src_addr = COMP_SA, .default_priority = 6, .self_reception = false };
    e32p_j1939_init(&node);
    e32p_j1939tp_config_t tc = { .src_addr = COMP_SA, .tx = comp_tp_tx, .on_rx = comp_tp_rx };
    e32p_j1939tp_init(&tc, &s_comp_tp);
    hil_emit("@EVT COMPANION ready SA=0x%02X", COMP_SA);

    uint32_t last_bcast = 0;
    uint8_t ctr = 0;
    for (;;) {
        comp_can_keepalive();   /* safety net: recover bus-off if it ever happens */
        e32p_twai_message_t  raw;
        e32p_j1939_message_t m = {0};
        bool got_nontp = false;
        bool got_j1939 = false;
        /* Raw receive, then route. e32p_j1939_receive() cannot be used here: it
           DEQUEUES an 11-bit frame and then discards it, so on a shared bus it
           would silently eat every diagnostics frame (ticket 3.1). */
        if (e32p_twai_receive(&raw, COMP_RX_TIMEOUT_MS) == ESP_OK) {
            if (hil_diag_is_isotp_frame(&raw)) {
                hil_diag_companion_on_frame(&raw);
            } else {
                /* The same decode e32p_j1939_receive() does after its 29-bit
                   check, kept identical so the J1939 path below is unchanged. */
                e32p_j1939_id_decode(raw.id, &m.priority, &m.pgn, &m.src_addr, &m.dst_addr);
                m.len = (raw.len <= 8u) ? raw.len : 8u;
                memcpy(m.data, raw.data, m.len);
                got_j1939 = true;
            }
        }
        if (got_j1939) {
            if (m.pgn == E32P_J1939_PGN_TP_CM || m.pgn == E32P_J1939_PGN_TP_DT) {
                /* TP frames arrive in a fast back-to-back burst. Feed the
                   reassembler WITHOUT a per-frame @EVT — the serial write would
                   stall the loop and overflow the RX queue, dropping packets. The
                   TP layer emits one @EVT TP for the whole reassembled message. */
                e32p_j1939tp_on_frame(s_comp_tp, &m);
            } else {
                hil_emit("@EVT J1939 pgn=%lu sa=0x%02X len=%u",
                         (unsigned long)m.pgn, m.src_addr, m.len);
                got_nontp = true;
            }
        }
        /* Read the clock after the receive, not before it. The ISO-TP peer paces
           its Consecutive Frames from this stamp; one taken before a 5 ms receive
           could be 5 ms old and let two CFs go out back to back. */
        const uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
        e32p_j1939tp_poll(s_comp_tp, now);
        hil_diag_companion_poll(now);
        /* Broadcast PGN 65262 ONLY in direct response to a NON-TP frame (e.g. the
           can_j1939_rx trigger), rate-limited. Never broadcasting into an idle bus
           keeps our error counters near zero (no bus-off, no disruptive reset), and
           never transmitting during a TP burst means we don't steal a receive slot
           and drop a packet. Single-shot: no auto-retransmit. */
        if (got_nontp && (now - last_bcast) >= 400) {
            last_bcast = now;
            uint32_t id = e32p_j1939_id_encode(6, DEMO_PGN, COMP_SA, E32P_J1939_ADDR_GLOBAL);
            twai_message_t tx = { .extd = 1, .ss = 1, .identifier = id,
                                  .data_length_code = 8,
                                  .data = { ctr, 1, 2, 3, 4, 5, 6, 7 } };
            twai_transmit(&tx, pdMS_TO_TICKS(20));
            ctr++;
        }
    }
}

void hil_can_start_companion(void)
{
    xTaskCreate(companion_task, "hil_can_comp", 4096, NULL, 5, NULL);
}
