/**
 * @file test_e32p_isotp.c
 * @brief Unity tests for e32p_isotp — fully headless. Two engines (A tester,
 *        B ECU) are wired through an in-process frame queue: A's tx sink
 *        enqueues for B and vice-versa, pump() drains the queue into the peer,
 *        and a virtual clock drives poll(). No CAN bus involved.
 *
 * Same harness shape as test_e32p_j1939tp.c.
 */
#include "unity.h"
#include <string.h>
#include "e32p_isotp.h"

#define A_TX 0x7E0u      /* A transmits here, B listens here */
#define B_TX 0x7E8u      /* B transmits here, A listens here */

static e32p_isotp_handle_t hA, hB;

/* Frame queue: to==0 -> deliver to A, to==1 -> deliver to B.
   Sized for a 4095-byte transfer (585 CFs) emitted in one poll. */
#define QN 2048
#define QM (QN - 1)
static struct { e32p_twai_message_t f; int to; } q[QN];
static int qh, qt;

/* Emission log. Counting queued frames does not work: pump() delivers a frame,
   the peer answers inside on_frame(), and the answer would be consumed by the
   same drain. So sinks record what was SENT, independent of what is pending. */
static int                 g_emit_n[4][2];
static e32p_twai_message_t g_emit_last[4][2];

static void log_emit(const e32p_twai_message_t *m, int to)
{
    e32p_isotp_pci_t p = e32p_isotp_pci_type(m->data, m->len);
    if (p >= 0 && p <= 3) { g_emit_n[p][to]++; g_emit_last[p][to] = *m; }
}
static int emitted(e32p_isotp_pci_t p, int to) { return g_emit_n[p][to]; }
static const e32p_twai_message_t *last_emitted(e32p_isotp_pci_t p, int to)
{
    return g_emit_n[p][to] ? &g_emit_last[p][to] : NULL;
}

static uint8_t  g_rx[4096];
static uint16_t g_rxlen;
static bool     g_rxdone;
static int      g_rxcount;
static bool     g_txdone;
static esp_err_t g_txstatus;

/* Sink back-pressure control: when >0, the next N emits from A are refused. */
static int g_a_refuse;

static esp_err_t txA(const e32p_twai_message_t *m, void *u)
{
    (void)u;
    if (g_a_refuse > 0) { g_a_refuse--; return ESP_FAIL; }
    log_emit(m, 1);
    q[qt].f = *m; q[qt].to = 1; qt = (qt + 1) & QM;
    return ESP_OK;
}
static esp_err_t txB(const e32p_twai_message_t *m, void *u)
{
    (void)u;
    log_emit(m, 0);
    q[qt].f = *m; q[qt].to = 0; qt = (qt + 1) & QM;
    return ESP_OK;
}

static void onrx(const uint8_t *d, uint16_t len, void *u)
{
    (void)u;
    if (len <= sizeof(g_rx)) { memcpy(g_rx, d, len); }
    g_rxlen = len; g_rxdone = true; g_rxcount++;
}
static void ontxdone(esp_err_t st, void *u)
{
    (void)u;
    g_txdone = true; g_txstatus = st;
}

/** @brief Deliver the frames pending at entry. Frames the peer emits while
 *         handling them stay queued for the next pump - a real bus does not
 *         deliver a reply inside the same transmission. */
static void pump(void)
{
    const int end = qt;
    while (qh != end) {
        e32p_twai_message_t f = q[qh].f;
        int to = q[qh].to;
        qh = (qh + 1) & QM;
        e32p_isotp_on_frame(to == 0 ? hA : hB, &f);
    }
}

/** @brief Run the pair forward: pump frames, tick both engines, repeat. */
static uint32_t g_clock_ms;

static void run(uint32_t ms, uint32_t step)
{
    uint32_t clock_ms = g_clock_ms;
    for (uint32_t t = 0; t < ms; t += step) {
        pump();
        e32p_isotp_poll(hA, clock_ms);
        e32p_isotp_poll(hB, clock_ms);
        pump();
        clock_ms += step;
    }
    g_clock_ms = clock_ms;
}

static void reset_world(void)
{
    qh = qt = 0;
    g_clock_ms = 0;
    memset(g_emit_n, 0, sizeof(g_emit_n));
    g_rxlen = 0; g_rxdone = false; g_rxcount = 0;
    g_txdone = false; g_txstatus = ESP_FAIL;
    g_a_refuse = 0;
    memset(g_rx, 0, sizeof(g_rx));
}

/** @brief Build a default A/B pair. @p bs / @p stmin are what B asks A for. */
static void make_pair(uint8_t bs, uint8_t stmin, uint16_t maxmsg, uint8_t wft)
{
    reset_world();
    e32p_isotp_config_t ca, cb;

    e32p_isotp_config_defaults(&ca, A_TX, B_TX, 0, 0, false);
    ca.mode = E32P_ISOTP_ADDR_NORMAL_11;
    ca.tx = txA; ca.on_rx = onrx; ca.on_tx_done = ontxdone;
    ca.max_msg_bytes = maxmsg;
    TEST_ASSERT_EQUAL(ESP_OK, e32p_isotp_init(&ca, &hA));

    e32p_isotp_config_defaults(&cb, B_TX, A_TX, 0, 0, false);
    cb.mode = E32P_ISOTP_ADDR_NORMAL_11;
    cb.tx = txB; cb.on_rx = onrx; cb.on_tx_done = ontxdone;
    cb.rx_block_size = bs;
    cb.rx_stmin_ms   = stmin;
    cb.rx_wft_max    = wft;
    cb.max_msg_bytes = maxmsg;
    TEST_ASSERT_EQUAL(ESP_OK, e32p_isotp_init(&cb, &hB));
}

static void destroy_pair(void)
{
    e32p_isotp_deinit(hA); hA = NULL;
    e32p_isotp_deinit(hB); hB = NULL;
}

/* ===================== 1-8: pure PCI codec, no engine =================== */

TEST_CASE("SF encodes length in the low nibble and pads to 0xAA", "[e32p_isotp]")
{
    make_pair(8, 0, 1024, 0);
    uint8_t payload[6] = { 1, 2, 3, 4, 5, 6 };
    TEST_ASSERT_EQUAL(ESP_OK, e32p_isotp_send(hA, payload, 6));

    const e32p_twai_message_t *sf = last_emitted(E32P_ISOTP_PCI_SF, 1);
    TEST_ASSERT_NOT_NULL(sf);
    TEST_ASSERT_EQUAL_HEX8(0x06, sf->data[0]);
    TEST_ASSERT_EQUAL(8, sf->len);              /* padded */
    TEST_ASSERT_EQUAL_HEX8(0xAA, sf->data[7]);  /* with the configured byte */
    destroy_pair();
}

TEST_CASE("SF with DLC 8 and SF_DL 3 ignores the padding bytes", "[e32p_isotp]")
{
    uint8_t d[8] = { 0x03, 0xDE, 0xAD, 0xBE, 0xAA, 0xAA, 0xAA, 0xAA };
    uint8_t len = 0;
    TEST_ASSERT_EQUAL(ESP_OK, e32p_isotp_pci_sf_len(d, 8, &len));
    TEST_ASSERT_EQUAL(3, len);
}

TEST_CASE("FF 12-bit length decodes across 8..4095", "[e32p_isotp]")
{
    uint16_t len = 0;
    uint8_t d8[8]    = { 0x10, 0x08 };
    uint8_t d4095[8] = { 0x1F, 0xFF };
    uint8_t d100[8]  = { 0x10, 0x64 };
    TEST_ASSERT_EQUAL(ESP_OK, e32p_isotp_pci_ff_len(d8, 8, &len));
    TEST_ASSERT_EQUAL(8, len);
    TEST_ASSERT_EQUAL(ESP_OK, e32p_isotp_pci_ff_len(d4095, 8, &len));
    TEST_ASSERT_EQUAL(4095, len);
    TEST_ASSERT_EQUAL(ESP_OK, e32p_isotp_pci_ff_len(d100, 8, &len));
    TEST_ASSERT_EQUAL(100, len);
}

TEST_CASE("FF with FF_DL 0 (32-bit escape) is rejected, not misparsed", "[e32p_isotp]")
{
    uint8_t d[8] = { 0x10, 0x00, 0, 0, 0x10, 0x00 };
    uint16_t len = 0xFFFF;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_isotp_pci_ff_len(d, 8, &len));
}

TEST_CASE("CF sequence number wraps 15 -> 0", "[e32p_isotp]")
{
    uint8_t sn = 0;
    uint8_t d15[8] = { 0x2F };
    uint8_t d0[8]  = { 0x20 };
    TEST_ASSERT_EQUAL(ESP_OK, e32p_isotp_pci_cf_sn(d15, 8, &sn));
    TEST_ASSERT_EQUAL(15, sn);
    TEST_ASSERT_EQUAL(ESP_OK, e32p_isotp_pci_cf_sn(d0, 8, &sn));
    TEST_ASSERT_EQUAL(0, sn);
}

TEST_CASE("FC decodes CTS / WAIT / OVFLW, BS and STmin", "[e32p_isotp]")
{
    e32p_isotp_fs_t fs; uint8_t bs, st;
    uint8_t cts[8]   = { 0x30, 0x08, 0x14 };
    uint8_t wait[8]  = { 0x31, 0x00, 0x00 };
    uint8_t ovflw[8] = { 0x32, 0x00, 0x00 };
    uint8_t bad[8]   = { 0x33, 0x00, 0x00 };

    TEST_ASSERT_EQUAL(ESP_OK, e32p_isotp_pci_fc(cts, 8, &fs, &bs, &st));
    TEST_ASSERT_EQUAL(E32P_ISOTP_FS_CTS, fs);
    TEST_ASSERT_EQUAL(8, bs);
    TEST_ASSERT_EQUAL_HEX8(0x14, st);
    TEST_ASSERT_EQUAL(ESP_OK, e32p_isotp_pci_fc(wait, 8, &fs, &bs, &st));
    TEST_ASSERT_EQUAL(E32P_ISOTP_FS_WAIT, fs);
    TEST_ASSERT_EQUAL(ESP_OK, e32p_isotp_pci_fc(ovflw, 8, &fs, &bs, &st));
    TEST_ASSERT_EQUAL(E32P_ISOTP_FS_OVFLW, fs);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_isotp_pci_fc(bad, 8, &fs, &bs, &st));
}

TEST_CASE("STmin 0xF1..0xF9 decode to 100..900us; reserved folds to 127ms",
          "[e32p_isotp]")
{
    TEST_ASSERT_EQUAL_UINT32(0,       e32p_isotp_stmin_us(0x00));
    TEST_ASSERT_EQUAL_UINT32(20000,   e32p_isotp_stmin_us(0x14));
    TEST_ASSERT_EQUAL_UINT32(127000,  e32p_isotp_stmin_us(0x7F));
    TEST_ASSERT_EQUAL_UINT32(100,     e32p_isotp_stmin_us(0xF1));
    TEST_ASSERT_EQUAL_UINT32(900,     e32p_isotp_stmin_us(0xF9));
    /* reserved ranges */
    TEST_ASSERT_EQUAL_UINT32(127000,  e32p_isotp_stmin_us(0x80));
    TEST_ASSERT_EQUAL_UINT32(127000,  e32p_isotp_stmin_us(0xF0));
    TEST_ASSERT_EQUAL_UINT32(127000,  e32p_isotp_stmin_us(0xFA));
    TEST_ASSERT_EQUAL_UINT32(127000,  e32p_isotp_stmin_us(0xFF));
}

TEST_CASE("normal-fixed 29-bit IDs compose to 0x18DAxxyy / 0x18DBxxyy",
          "[e32p_isotp]")
{
    /* ISO 15765-4 external test equipment: SA 0xF1, ECU 0x10. */
    TEST_ASSERT_EQUAL_HEX32(0x18DA10F1u, e32p_isotp_id_normal_fixed(0xF1, 0x10, false));
    TEST_ASSERT_EQUAL_HEX32(0x18DAF110u, e32p_isotp_id_normal_fixed(0x10, 0xF1, false));
    TEST_ASSERT_EQUAL_HEX32(0x18DB33F1u, e32p_isotp_id_normal_fixed(0xF1, 0x33, true));
    /* PF byte must be 0xDA/0xDB - disjoint from every J1939 PGN in this repo. */
    TEST_ASSERT_EQUAL_HEX8(0xDA, (e32p_isotp_id_normal_fixed(1, 2, false) >> 16) & 0xFF);
    TEST_ASSERT_EQUAL_HEX8(0xDB, (e32p_isotp_id_normal_fixed(1, 2, true) >> 16) & 0xFF);
}

/* ===================== 9-11: end-to-end round trips ===================== */

TEST_CASE("a 6-byte payload goes out as exactly one SF", "[e32p_isotp]")
{
    make_pair(8, 0, 1024, 0);
    uint8_t p[6] = { 0xC0, 0xFF, 0xEE, 0x11, 0x22, 0x33 };
    TEST_ASSERT_EQUAL(ESP_OK, e32p_isotp_send(hA, p, 6));
    TEST_ASSERT_EQUAL(1, emitted(E32P_ISOTP_PCI_SF, 1));
    TEST_ASSERT_EQUAL(0, emitted(E32P_ISOTP_PCI_FF, 1));
    run(50, 1);
    TEST_ASSERT_TRUE(g_rxdone);
    TEST_ASSERT_EQUAL(6, g_rxlen);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(p, g_rx, 6);
    destroy_pair();
}

TEST_CASE("100-byte round-trip is byte-identical", "[e32p_isotp]")
{
    make_pair(8, 0, 1024, 0);
    uint8_t p[100];
    for (int i = 0; i < 100; i++) { p[i] = (uint8_t)(i * 7 + 3); }
    TEST_ASSERT_EQUAL(ESP_OK, e32p_isotp_send(hA, p, 100));
    run(500, 1);
    TEST_ASSERT_TRUE(g_rxdone);
    TEST_ASSERT_EQUAL(100, g_rxlen);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(p, g_rx, 100);
    TEST_ASSERT_TRUE(g_txdone);
    TEST_ASSERT_EQUAL(ESP_OK, g_txstatus);
    destroy_pair();
}

TEST_CASE("4095-byte round-trip is byte-identical", "[e32p_isotp]")
{
    make_pair(0, 0, 4095, 0);       /* BS 0: no further FC after the first */
    static uint8_t p[4095];
    for (int i = 0; i < 4095; i++) { p[i] = (uint8_t)(i ^ (i >> 8)); }
    TEST_ASSERT_EQUAL(ESP_OK, e32p_isotp_send(hA, p, 4095));
    run(4000, 1);
    TEST_ASSERT_TRUE(g_rxdone);
    TEST_ASSERT_EQUAL(4095, g_rxlen);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(p, g_rx, 4095);
    destroy_pair();
}

/* ===================== 12-15: flow control on TX ======================== */

TEST_CASE("sender honours peer BS=4 and waits for FC every 4 CFs", "[e32p_isotp]")
{
    make_pair(4, 0, 1024, 0);
    uint8_t p[100];
    memset(p, 0x5A, sizeof(p));
    TEST_ASSERT_EQUAL(ESP_OK, e32p_isotp_send(hA, p, 100));

    /* FF out, B's FC back, then exactly 4 CFs before A must stop. */
    pump();
    e32p_isotp_poll(hA, 0); e32p_isotp_poll(hB, 0);
    pump();
    e32p_isotp_poll(hA, 0);
    TEST_ASSERT_EQUAL(4, emitted(E32P_ISOTP_PCI_CF, 1));
    TEST_ASSERT_TRUE(e32p_isotp_tx_busy(hA));

    run(500, 1);
    TEST_ASSERT_TRUE(g_rxdone);
    TEST_ASSERT_EQUAL(100, g_rxlen);
    /* 100 B = 6 in the FF + 94 over 14 CFs -> 4 blocks -> 4 FCs from B. */
    destroy_pair();
}

TEST_CASE("sender honours peer STmin and paces CFs", "[e32p_isotp]")
{
    make_pair(0, 20, 1024, 0);      /* BS 0, STmin 20 ms */
    uint8_t p[50];
    memset(p, 0x11, sizeof(p));
    TEST_ASSERT_EQUAL(ESP_OK, e32p_isotp_send(hA, p, 50));

    run(30, 1);                     /* ~1 CF per 20 ms */
    const int early = emitted(E32P_ISOTP_PCI_CF, 1) + g_rxcount;
    TEST_ASSERT_TRUE(e32p_isotp_tx_busy(hA));
    TEST_ASSERT_LESS_THAN(4, early);

    run(400, 1);
    TEST_ASSERT_TRUE(g_rxdone);
    TEST_ASSERT_EQUAL(50, g_rxlen);
    destroy_pair();
}

TEST_CASE("receiver emits FC.WAIT up to wft_max then proceeds", "[e32p_isotp]")
{
    make_pair(2, 0, 1024, 1);       /* BS 2, one WAIT allowed per block */
    uint8_t p[60];
    memset(p, 0x77, sizeof(p));
    TEST_ASSERT_EQUAL(ESP_OK, e32p_isotp_send(hA, p, 60));
    run(600, 1);
    TEST_ASSERT_TRUE(g_rxdone);
    TEST_ASSERT_EQUAL(60, g_rxlen);
    destroy_pair();
}

TEST_CASE("FC.OVFLW aborts the sender with ESP_ERR_NO_MEM", "[e32p_isotp]")
{
    /* B can hold only 64 B; A announces 200 -> B must answer FC.OVFLW. */
    reset_world();
    e32p_isotp_config_t ca, cb;
    e32p_isotp_config_defaults(&ca, A_TX, B_TX, 0, 0, false);
    ca.tx = txA; ca.on_rx = onrx; ca.on_tx_done = ontxdone; ca.max_msg_bytes = 1024;
    TEST_ASSERT_EQUAL(ESP_OK, e32p_isotp_init(&ca, &hA));
    e32p_isotp_config_defaults(&cb, B_TX, A_TX, 0, 0, false);
    cb.tx = txB; cb.on_rx = onrx; cb.max_msg_bytes = 64;
    TEST_ASSERT_EQUAL(ESP_OK, e32p_isotp_init(&cb, &hB));

    uint8_t p[200];
    memset(p, 0x42, sizeof(p));
    TEST_ASSERT_EQUAL(ESP_OK, e32p_isotp_send(hA, p, 200));
    run(200, 1);
    TEST_ASSERT_TRUE(g_txdone);
    TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM, g_txstatus);
    TEST_ASSERT_FALSE(g_rxdone);
    destroy_pair();
}

/* ===================== 16-19: timers and error paths ==================== */

TEST_CASE("N_Bs expiry (no FC) fails the transfer with ESP_ERR_TIMEOUT",
          "[e32p_isotp]")
{
    make_pair(8, 0, 1024, 0);
    uint8_t p[100];
    memset(p, 0x33, sizeof(p));
    TEST_ASSERT_EQUAL(ESP_OK, e32p_isotp_send(hA, p, 100));

    /* Drop everything heading to B, so no FC ever comes back. */
    qh = qt = 0;
    for (uint32_t t = 0; t <= 1100; t += 10) {
        qh = qt = 0;
        e32p_isotp_poll(hA, t);
    }
    TEST_ASSERT_TRUE(g_txdone);
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, g_txstatus);
    TEST_ASSERT_FALSE(e32p_isotp_tx_busy(hA));
    destroy_pair();
}

TEST_CASE("N_Cr expiry (missing CF) discards the RX session", "[e32p_isotp]")
{
    make_pair(8, 0, 1024, 0);
    /* Hand B a FirstFrame directly, then never send a CF. */
    e32p_twai_message_t ff = { .id = A_TX, .len = 8,
                               .data = { 0x10, 0x64, 1, 2, 3, 4, 5, 6 } };
    e32p_isotp_poll(hB, 0);
    TEST_ASSERT_EQUAL(ESP_OK, e32p_isotp_on_frame(hB, &ff));
    TEST_ASSERT_TRUE(e32p_isotp_rx_busy(hB));

    e32p_isotp_poll(hB, 1100);
    TEST_ASSERT_FALSE(e32p_isotp_rx_busy(hB));
    TEST_ASSERT_FALSE(g_rxdone);
    destroy_pair();
}

TEST_CASE("out-of-order CF aborts the RX session", "[e32p_isotp]")
{
    make_pair(8, 0, 1024, 0);
    e32p_twai_message_t ff = { .id = A_TX, .len = 8,
                               .data = { 0x10, 0x64, 1, 2, 3, 4, 5, 6 } };
    e32p_twai_message_t bad_cf = { .id = A_TX, .len = 8,
                                   .data = { 0x23, 9, 9, 9, 9, 9, 9, 9 } };
    e32p_isotp_poll(hB, 0);
    e32p_isotp_on_frame(hB, &ff);
    TEST_ASSERT_TRUE(e32p_isotp_rx_busy(hB));
    e32p_isotp_on_frame(hB, &bad_cf);      /* expected SN 1, got 3 */
    TEST_ASSERT_FALSE(e32p_isotp_rx_busy(hB));
    TEST_ASSERT_FALSE(g_rxdone);
    destroy_pair();
}

TEST_CASE("a new FF during an active RX session restarts it", "[e32p_isotp]")
{
    make_pair(8, 0, 1024, 0);
    e32p_twai_message_t ff1 = { .id = A_TX, .len = 8,
                                .data = { 0x10, 0x64, 1, 2, 3, 4, 5, 6 } };
    e32p_twai_message_t ff2 = { .id = A_TX, .len = 8,
                                .data = { 0x10, 0x0A, 0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5 } };
    e32p_twai_message_t cf  = { .id = A_TX, .len = 8,
                                .data = { 0x21, 0xA6, 0xA7, 0xA8, 0xA9, 0, 0, 0 } };
    e32p_isotp_poll(hB, 0);
    e32p_isotp_on_frame(hB, &ff1);
    e32p_isotp_on_frame(hB, &ff2);         /* restarts: now expecting 10 bytes */
    e32p_isotp_on_frame(hB, &cf);
    TEST_ASSERT_TRUE(g_rxdone);
    TEST_ASSERT_EQUAL(10, g_rxlen);
    TEST_ASSERT_EQUAL_HEX8(0xA0, g_rx[0]);
    TEST_ASSERT_EQUAL_HEX8(0xA9, g_rx[9]);
    destroy_pair();
}

/* ===================== 20-22: filtering, bounds, vectors ================ */

TEST_CASE("frames with a foreign CAN ID are ignored", "[e32p_isotp]")
{
    make_pair(8, 0, 1024, 0);
    e32p_twai_message_t alien = { .id = 0x123, .len = 8,
                                  .data = { 0x06, 1, 2, 3, 4, 5, 6, 0 } };
    TEST_ASSERT_EQUAL(ESP_OK, e32p_isotp_on_frame(hB, &alien));
    TEST_ASSERT_FALSE(g_rxdone);
    TEST_ASSERT_FALSE(e32p_isotp_rx_busy(hB));
    destroy_pair();
}

TEST_CASE("send longer than max_msg_bytes returns ESP_ERR_INVALID_ARG",
          "[e32p_isotp]")
{
    make_pair(8, 0, 64, 0);
    uint8_t p[200];
    memset(p, 1, sizeof(p));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_isotp_send(hA, p, 200));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_isotp_send(hA, p, 0));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_isotp_send(hA, NULL, 10));
    TEST_ASSERT_FALSE(e32p_isotp_tx_busy(hA));
    destroy_pair();
}

TEST_CASE("reference vector: FF/FC/CF byte sequence matches ISO 15765-2",
          "[e32p_isotp]")
{
    /* Canonical 20-byte transfer. FF carries 6 bytes and declares 0x014;
       B answers FC.CTS BS=8 STmin=1; two CFs carry the remaining 14. */
    make_pair(8, 1, 1024, 0);
    uint8_t p[20];
    for (int i = 0; i < 20; i++) { p[i] = (uint8_t)(0xA0 + i); }
    TEST_ASSERT_EQUAL(ESP_OK, e32p_isotp_send(hA, p, 20));

    const e32p_twai_message_t *ff = last_emitted(E32P_ISOTP_PCI_FF, 1);
    TEST_ASSERT_NOT_NULL(ff);
    TEST_ASSERT_EQUAL_HEX8(0x10, ff->data[0]);
    TEST_ASSERT_EQUAL_HEX8(0x14, ff->data[1]);
    TEST_ASSERT_EQUAL_HEX8(0xA0, ff->data[2]);
    TEST_ASSERT_EQUAL_HEX8(0xA5, ff->data[7]);

    pump();
    e32p_isotp_poll(hB, 0);
    const e32p_twai_message_t *fc = last_emitted(E32P_ISOTP_PCI_FC, 0);
    TEST_ASSERT_NOT_NULL(fc);
    TEST_ASSERT_EQUAL_HEX8(0x30, fc->data[0]);
    TEST_ASSERT_EQUAL_HEX8(0x08, fc->data[1]);
    TEST_ASSERT_EQUAL_HEX8(0x01, fc->data[2]);

    run(300, 1);
    TEST_ASSERT_TRUE(g_rxdone);
    TEST_ASSERT_EQUAL(20, g_rxlen);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(p, g_rx, 20);
    destroy_pair();
}

/* ===================== 23-26: review-driven additions =================== */

TEST_CASE("BS=0 sends all remaining CFs with no further FC", "[e32p_isotp]")
{
    make_pair(0, 0, 1024, 0);       /* BS 0 = what most PC stacks send */
    uint8_t p[200];
    for (int i = 0; i < 200; i++) { p[i] = (uint8_t)i; }
    TEST_ASSERT_EQUAL(ESP_OK, e32p_isotp_send(hA, p, 200));

    /* Exactly one FC from B for the whole transfer. */
    run(400, 1);
    TEST_ASSERT_TRUE(g_rxdone);
    TEST_ASSERT_EQUAL(200, g_rxlen);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(p, g_rx, 200);
    destroy_pair();
}

TEST_CASE("STmin=0 completes without waiting on the poll cadence",
          "[e32p_isotp]")
{
    make_pair(0, 0, 1024, 0);
    uint8_t p[300];
    memset(p, 0x5C, sizeof(p));
    TEST_ASSERT_EQUAL(ESP_OK, e32p_isotp_send(hA, p, 300));

    /* One pump + one poll pair must be enough: with STmin 0 and BS 0 the
       whole remainder is emitted in a single poll, not one CF per tick. */
    pump();
    e32p_isotp_poll(hA, 0); e32p_isotp_poll(hB, 0);
    pump();
    e32p_isotp_poll(hA, 0); e32p_isotp_poll(hB, 0);
    pump();
    TEST_ASSERT_TRUE(g_rxdone);
    TEST_ASSERT_EQUAL(300, g_rxlen);
    destroy_pair();
}

TEST_CASE("a peer's unpadded SF and CF are accepted", "[e32p_isotp]")
{
    make_pair(8, 0, 1024, 0);
    /* DLC equal to the actual length - what many PC stacks emit. */
    e32p_twai_message_t sf = { .id = A_TX, .len = 4, .data = { 0x03, 0x11, 0x22, 0x33 } };
    TEST_ASSERT_EQUAL(ESP_OK, e32p_isotp_on_frame(hB, &sf));
    TEST_ASSERT_TRUE(g_rxdone);
    TEST_ASSERT_EQUAL(3, g_rxlen);
    TEST_ASSERT_EQUAL_HEX8(0x11, g_rx[0]);

    g_rxdone = false;
    e32p_twai_message_t ff = { .id = A_TX, .len = 8,
                               .data = { 0x10, 0x09, 1, 2, 3, 4, 5, 6 } };
    e32p_twai_message_t cf = { .id = A_TX, .len = 4, .data = { 0x21, 7, 8, 9 } };
    e32p_isotp_poll(hB, 0);
    e32p_isotp_on_frame(hB, &ff);
    e32p_isotp_on_frame(hB, &cf);
    TEST_ASSERT_TRUE(g_rxdone);
    TEST_ASSERT_EQUAL(9, g_rxlen);
    TEST_ASSERT_EQUAL_HEX8(9, g_rx[8]);
    destroy_pair();
}

TEST_CASE("poll() emits multiple CFs in one invocation when credit allows",
          "[e32p_isotp]")
{
    make_pair(0, 0, 1024, 0);
    uint8_t p[100];
    memset(p, 0x9E, sizeof(p));
    TEST_ASSERT_EQUAL(ESP_OK, e32p_isotp_send(hA, p, 100));

    pump();                          /* FF -> B */
    e32p_isotp_poll(hB, 0);          /* B answers FC */
    pump();                          /* FC -> A */
    e32p_isotp_poll(hA, 0);          /* ONE poll: A must emit every CF */
    TEST_ASSERT_GREATER_THAN(1, emitted(E32P_ISOTP_PCI_CF, 1));
    TEST_ASSERT_FALSE(e32p_isotp_tx_busy(hA));
    destroy_pair();
}

/* ===================== 27-29: FC emission and concurrency =============== */

TEST_CASE("receiver emits FC.CTS with its own configured BS and STmin",
          "[e32p_isotp]")
{
    make_pair(5, 30, 1024, 0);       /* B advertises BS 5, STmin 30 ms */
    uint8_t p[100];
    memset(p, 0x2B, sizeof(p));
    TEST_ASSERT_EQUAL(ESP_OK, e32p_isotp_send(hA, p, 100));

    pump();
    e32p_isotp_poll(hB, 0);
    const e32p_twai_message_t *fc = last_emitted(E32P_ISOTP_PCI_FC, 0);
    TEST_ASSERT_NOT_NULL(fc);
    TEST_ASSERT_EQUAL_HEX8(0x30, fc->data[0]);   /* CTS */
    TEST_ASSERT_EQUAL_HEX8(5,    fc->data[1]);   /* our BS, not the peer's */
    TEST_ASSERT_EQUAL_HEX8(30,   fc->data[2]);   /* our STmin */
    destroy_pair();
}

TEST_CASE("receiver emits no further FC after the final block", "[e32p_isotp]")
{
    make_pair(4, 0, 1024, 0);
    uint8_t p[30];                   /* 6 in FF + 24 over 4 CFs = one block */
    memset(p, 0x6D, sizeof(p));
    TEST_ASSERT_EQUAL(ESP_OK, e32p_isotp_send(hA, p, 30));
    run(300, 1);
    TEST_ASSERT_TRUE(g_rxdone);
    TEST_ASSERT_EQUAL(30, g_rxlen);

    /* One block, so exactly one FC: the opening CTS. A second would mean B
       asked for more after the payload was already complete. */
    TEST_ASSERT_EQUAL(1, emitted(E32P_ISOTP_PCI_FC, 0));
    TEST_ASSERT_FALSE(e32p_isotp_rx_busy(hB));
    destroy_pair();
}

TEST_CASE("concurrent TX and RX sessions on one channel do not interfere",
          "[e32p_isotp]")
{
    make_pair(0, 0, 1024, 0);
    /* A starts sending 100 B to B, while B simultaneously sends 80 B to A. */
    uint8_t pa[100], pb[80];
    for (int i = 0; i < 100; i++) { pa[i] = (uint8_t)(0x10 + i); }
    for (int i = 0; i < 80;  i++) { pb[i] = (uint8_t)(0x80 + i); }

    TEST_ASSERT_EQUAL(ESP_OK, e32p_isotp_send(hA, pa, 100));
    TEST_ASSERT_EQUAL(ESP_OK, e32p_isotp_send(hB, pb, 80));
    TEST_ASSERT_TRUE(e32p_isotp_tx_busy(hA));
    TEST_ASSERT_TRUE(e32p_isotp_tx_busy(hB));

    run(600, 1);

    /* Both payloads must have been delivered - two distinct on_rx calls. */
    TEST_ASSERT_EQUAL(2, g_rxcount);
    TEST_ASSERT_FALSE(e32p_isotp_tx_busy(hA));
    TEST_ASSERT_FALSE(e32p_isotp_tx_busy(hB));
    destroy_pair();
}

/* ===================== extra: first poll after a long uptime ============= */

TEST_CASE("timers armed before the first poll are not treated as already expired",
          "[e32p_isotp]")
{
    /* A board can create a channel and send long after boot. The engine has no
       clock of its own, so a session armed before the channel's first poll() is
       stamped from time 0. A first poll at a large uptime must not expire that
       session on the spot - on hardware this made every multi-frame transfer
       fail with ESP_ERR_TIMEOUT within a few ms. */
    make_pair(8, 0, 1024, 0);
    g_clock_ms = 50000;                  /* both engines are first polled at 50 s */
    uint8_t p[100];
    for (int i = 0; i < 100; i++) { p[i] = (uint8_t)(i + 1); }
    TEST_ASSERT_EQUAL(ESP_OK, e32p_isotp_send(hA, p, 100));
    run(500, 1);
    TEST_ASSERT_TRUE(g_txdone);
    TEST_ASSERT_EQUAL(ESP_OK, g_txstatus);
    TEST_ASSERT_TRUE(g_rxdone);
    TEST_ASSERT_EQUAL(100, g_rxlen);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(p, g_rx, 100);
    destroy_pair();
}

/* ===================== extra: sink back-pressure ======================== */

TEST_CASE("a refusing sink is back-pressure, not an error", "[e32p_isotp]")
{
    make_pair(0, 0, 1024, 0);
    uint8_t p[100];
    memset(p, 0x4F, sizeof(p));
    TEST_ASSERT_EQUAL(ESP_OK, e32p_isotp_send(hA, p, 100));

    pump();                          /* FF -> B */
    e32p_isotp_poll(hB, 0);          /* B queues its FC */
    g_a_refuse = 1;                  /* arm BEFORE A's first CF */
    pump();                          /* FC -> A */
    e32p_isotp_poll(hA, 0);          /* A's first CF is refused */
    TEST_ASSERT_EQUAL(0, g_a_refuse);            /* the refusal really happened */
    TEST_ASSERT_TRUE(e32p_isotp_tx_busy(hA));   /* not failed - just held up */

    run(400, 1);
    TEST_ASSERT_TRUE(g_rxdone);
    TEST_ASSERT_EQUAL(100, g_rxlen);
    TEST_ASSERT_EQUAL(ESP_OK, g_txstatus);
    destroy_pair();
}

/* ===================== extra: STmin on the wire (LA2016) ================= */

TEST_CASE("after a late Flow Control the second CF still waits STmin", "[e32p_isotp]")
{
    /* On the bus the first two CFs of every block went out 12 us apart, although
       the peer asked for STmin 1 ms. The FC was handled with the clock of the
       poll BEFORE it arrived, so the next poll found STmin already over. Here
       the FC reaches A 15 ms after A's last poll. */
    make_pair(8, 20, 1024, 0);              /* B asks for STmin 20 ms */
    uint8_t p[100];
    memset(p, 0x3C, sizeof(p));
    e32p_isotp_poll(hA, 1000); e32p_isotp_poll(hB, 1000);
    TEST_ASSERT_EQUAL(ESP_OK, e32p_isotp_send(hA, p, 100));
    pump();                                 /* FF -> B, B queues its FC */
    pump();                                 /* FC -> A, 15 ms after A's last poll */
    e32p_isotp_poll(hA, 1015);              /* the first CF may go now */
    TEST_ASSERT_EQUAL(1, emitted(E32P_ISOTP_PCI_CF, 1));
    e32p_isotp_poll(hA, 1020);              /* 5 ms after CF 1: too early */
    TEST_ASSERT_EQUAL(1, emitted(E32P_ISOTP_PCI_CF, 1));
    e32p_isotp_poll(hA, 1034);              /* 19 ms after CF 1: still too early */
    TEST_ASSERT_EQUAL(1, emitted(E32P_ISOTP_PCI_CF, 1));
    e32p_isotp_poll(hA, 1036);
    TEST_ASSERT_EQUAL(2, emitted(E32P_ISOTP_PCI_CF, 1));

    g_clock_ms = 1036;
    run(600, 1);
    TEST_ASSERT_TRUE(g_rxdone);
    TEST_ASSERT_EQUAL(100, g_rxlen);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(p, g_rx, 100);
    destroy_pair();
}

TEST_CASE("CF pacing keeps a full STmin when a poll lands late in a millisecond",
          "[e32p_isotp]")
{
    /* poll() only sees whole milliseconds. A CF sent at 2000.9 ms is stamped
       2000, so "now + STmin" would release the next CF at 2001.0 ms, 0.1 ms
       later. On the bus the DUT sent CFs 0.56 ms apart with STmin 1 ms. */
    make_pair(0, 1, 1024, 0);               /* BS 0, STmin 1 ms */
    uint8_t p[100];
    memset(p, 0x5E, sizeof(p));
    e32p_isotp_poll(hA, 2000); e32p_isotp_poll(hB, 2000);
    TEST_ASSERT_EQUAL(ESP_OK, e32p_isotp_send(hA, p, 100));
    pump();                                 /* FF -> B, B queues its FC */
    pump();                                 /* FC -> A */
    e32p_isotp_poll(hA, 2000);
    const int first = emitted(E32P_ISOTP_PCI_CF, 1);
    TEST_ASSERT_EQUAL(1, first);
    e32p_isotp_poll(hA, 2001);              /* may be only 0.1 ms after CF 1 */
    TEST_ASSERT_EQUAL(first, emitted(E32P_ISOTP_PCI_CF, 1));
    e32p_isotp_poll(hA, 2002);
    TEST_ASSERT_EQUAL(first + 1, emitted(E32P_ISOTP_PCI_CF, 1));

    g_clock_ms = 2002;
    run(200, 1);
    TEST_ASSERT_TRUE(g_rxdone);
    TEST_ASSERT_EQUAL(100, g_rxlen);
    destroy_pair();
}
