/**
 * @file test_e32p_j1939tp.c
 * @brief Unity tests for e32p_j1939tp — fully headless. Two engines (A, B) are
 *        wired together through an in-process frame queue: A's tx sink enqueues
 *        for B and vice-versa, a pump() drains the queue into the peer, and a
 *        virtual clock drives poll(). No CAN bus involved.
 */
#include "unity.h"
#include <string.h>
#include "esp_heap_caps.h"
#include "e32p_j1939tp.h"

#define A_SA 0x80u
#define B_SA 0x21u
#define TEST_PGN 65260u  /* 0xFEEC, PDU2 */
#define TEST_DT_PAYLOAD 7u   /* data bytes per TP.DT frame (engine TP_DT_PAYLOAD is private) */

static e32p_j1939tp_handle_t hA, hB;

/* Frame queue: to==0 -> deliver to A, to==1 -> deliver to B. */
static struct { e32p_j1939_message_t f; int to; } q[512];
static int qh, qt;

static uint8_t  g_rx[2048];
static uint16_t g_rxlen;
static uint32_t g_rxpgn;
static uint8_t  g_rxsrc;
static bool     g_rxdone;
static bool     g_txdone;
static esp_err_t g_txstatus;

static esp_err_t txA(const e32p_j1939_message_t *m, void *u) { (void)u; q[qt].f = *m; q[qt].to = 1; qt = (qt + 1) & 511; return ESP_OK; }
static esp_err_t txB(const e32p_j1939_message_t *m, void *u) { (void)u; q[qt].f = *m; q[qt].to = 0; qt = (qt + 1) & 511; return ESP_OK; }

static void onrx(uint32_t pgn, uint8_t src, uint8_t dst, const uint8_t *d, uint16_t len, void *u)
{
    (void)dst; (void)u;
    memcpy(g_rx, d, len);
    g_rxlen = len; g_rxpgn = pgn; g_rxsrc = src; g_rxdone = true;
}
static void ontxdone(uint32_t pgn, uint8_t dst, esp_err_t st, void *u)
{
    (void)pgn; (void)dst; (void)u;
    g_txdone = true; g_txstatus = st;
}

static void pump(void)
{
    while (qh != qt) {
        e32p_j1939_message_t f = q[qh].f;
        int to = q[qh].to;
        qh = (qh + 1) & 511;
        e32p_j1939tp_on_frame(to == 0 ? hA : hB, &f);
    }
}

static bool q_has_abort(void)
{
    for (int i = qh; i != qt; i = (i + 1) & 511) {
        if (q[i].f.pgn == E32P_J1939_PGN_TP_CM && q[i].f.data[0] == 0xFFu) {
            return true;
        }
    }
    return false;
}

static void harness_reset(void)
{
    qh = qt = 0;
    g_rxlen = 0; g_rxpgn = 0; g_rxsrc = 0; g_rxdone = false;
    g_txdone = false; g_txstatus = ESP_FAIL;
    memset(g_rx, 0, sizeof(g_rx));
}

static void make_engines(void)
{
    e32p_j1939tp_config_t ca = { .src_addr = A_SA, .tx = txA, .on_rx = onrx,
                                 .on_tx_done = ontxdone, .user = NULL };
    e32p_j1939tp_config_t cb = { .src_addr = B_SA, .tx = txB, .on_rx = onrx,
                                 .on_tx_done = NULL, .user = NULL };
    TEST_ESP_OK(e32p_j1939tp_init(&ca, &hA));
    TEST_ESP_OK(e32p_j1939tp_init(&cb, &hB));
}
static void drop_engines(void)
{
    e32p_j1939tp_deinit(hA); hA = NULL;
    e32p_j1939tp_deinit(hB); hB = NULL;
}

TEST_CASE("e32p_j1939tp: BAM round-trip reassembles byte-identical", "[e32p_j1939tp]")
{
    harness_reset();
    make_engines();

    uint8_t msg[100];
    for (int i = 0; i < 100; i++) msg[i] = (uint8_t)(i * 3 + 1);

    TEST_ESP_OK(e32p_j1939tp_send(hA, TEST_PGN, 6, E32P_J1939_ADDR_GLOBAL, msg, 100));

    for (uint32_t t = 0; t < 6000 && !g_rxdone; t += 25) {
        e32p_j1939tp_poll(hA, t); pump();
        e32p_j1939tp_poll(hB, t); pump();
    }

    TEST_ASSERT_TRUE(g_rxdone);
    TEST_ASSERT_EQUAL_UINT16(100, g_rxlen);
    TEST_ASSERT_EQUAL_UINT32(TEST_PGN, g_rxpgn);
    TEST_ASSERT_EQUAL_HEX8(A_SA, g_rxsrc);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(msg, g_rx, 100);
    drop_engines();
}

TEST_CASE("e32p_j1939tp: RTS/CTS multi-burst round-trip (200B)", "[e32p_j1939tp]")
{
    harness_reset();
    make_engines();

    uint8_t msg[200];               /* 29 packets -> 2 CTS bursts (16 + 13) */
    for (int i = 0; i < 200; i++) msg[i] = (uint8_t)(255 - i);

    TEST_ESP_OK(e32p_j1939tp_send(hA, TEST_PGN, 6, B_SA, msg, 200));

    for (uint32_t t = 0; t < 4000 && !g_txdone; t += 10) {
        e32p_j1939tp_poll(hA, t); pump();
        e32p_j1939tp_poll(hB, t); pump();
    }

    TEST_ASSERT_TRUE(g_rxdone);
    TEST_ASSERT_EQUAL_UINT16(200, g_rxlen);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(msg, g_rx, 200);
    TEST_ASSERT_TRUE(g_txdone);
    TEST_ASSERT_EQUAL(ESP_OK, g_txstatus);   /* EndOfMsgAck received */
    drop_engines();
}

TEST_CASE("e32p_j1939tp: receiver aborts on T1 inter-packet timeout", "[e32p_j1939tp]")
{
    harness_reset();
    make_engines();

    uint8_t msg[100];
    memset(msg, 0xA5, sizeof(msg));
    TEST_ESP_OK(e32p_j1939tp_send(hA, TEST_PGN, 6, B_SA, msg, 100));

    e32p_j1939tp_poll(hA, 0); pump();   /* A -> RTS -> B */
    e32p_j1939tp_poll(hB, 0); pump();   /* B -> CTS -> A (A now ready to send) */

    /* Starve the receiver: never let A send its TP.DT, advance B past T1=750ms. */
    e32p_j1939tp_poll(hB, 1000);

    TEST_ASSERT_TRUE(q_has_abort());    /* B emitted a Conn_Abort */
    TEST_ASSERT_FALSE(g_rxdone);
    drop_engines();
}

TEST_CASE("e32p_j1939tp: rejects bad args", "[e32p_j1939tp]")
{
    harness_reset();
    make_engines();

    uint8_t small[4] = {1, 2, 3, 4};
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      e32p_j1939tp_send(hA, TEST_PGN, 6, B_SA, small, 4));   /* len < 9 */
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      e32p_j1939tp_send(hA, TEST_PGN, 6, B_SA, NULL, 100));

    e32p_j1939tp_handle_t h = NULL;
    e32p_j1939tp_config_t bad = { .src_addr = A_SA, .tx = NULL, .on_rx = onrx };
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_j1939tp_init(&bad, &h));
    drop_engines();
}

/* --- Hostile inbound TP.CM / TP.DT (audit SEC-06) --------------------------
 * Frames are crafted by hand and fed straight into engine B, as if a peer with
 * source address A_SA had sent them. B's replies land in the frame queue. */

static bool q_has_cm(uint8_t ctrl)
{
    for (int i = qh; i != qt; i = (i + 1) & 511) {
        if (q[i].f.pgn == E32P_J1939_PGN_TP_CM && q[i].f.data[0] == ctrl) {
            return true;
        }
    }
    return false;
}

static void feed_cm(uint8_t ctrl, uint8_t dst, uint16_t size, uint8_t packets)
{
    e32p_j1939_message_t f = { .priority = 7, .pgn = E32P_J1939_PGN_TP_CM,
                               .src_addr = A_SA, .dst_addr = dst, .len = 8 };
    f.data[0] = ctrl;
    f.data[1] = (uint8_t)(size & 0xFFu);
    f.data[2] = (uint8_t)(size >> 8);
    f.data[3] = packets;
    f.data[4] = 0xFF;
    f.data[5] = (uint8_t)(TEST_PGN & 0xFFu);
    f.data[6] = (uint8_t)((TEST_PGN >> 8) & 0xFFu);
    f.data[7] = (uint8_t)((TEST_PGN >> 16) & 0xFFu);
    TEST_ESP_OK(e32p_j1939tp_on_frame(hB, &f));
}

static void feed_dt(uint8_t seq, uint8_t fill)
{
    e32p_j1939_message_t f = { .priority = 7, .pgn = E32P_J1939_PGN_TP_DT,
                               .src_addr = A_SA, .dst_addr = E32P_J1939_ADDR_GLOBAL, .len = 8 };
    f.data[0] = seq;
    memset(&f.data[1], fill, TEST_DT_PAYLOAD);
    TEST_ESP_OK(e32p_j1939tp_on_frame(hB, &f));
}

/* Do NOT run against the pre-SEC-06 engine: DT seq 3 writes out of bounds there. */
TEST_CASE("e32p_j1939tp: BAM size 9 with 3 packets is rejected, DT seq 3 ignored", "[e32p_j1939tp]")
{
    harness_reset();
    make_engines();

    /* ceil(9/7) = 2, so 3 is a lie. Before the fix, DT seq 3 gave off=14, n=65531. */
    feed_cm(0x20, E32P_J1939_ADDR_GLOBAL, 9, 3);
    feed_dt(1, 0x11);
    feed_dt(2, 0x22);
    feed_dt(3, 0x33);
    e32p_j1939tp_poll(hB, 100);

    TEST_ASSERT_FALSE(g_rxdone);
    TEST_ASSERT_EQUAL_UINT16(0, g_rxlen);
    TEST_ASSERT_TRUE(heap_caps_check_integrity_all(true));   /* no heap overrun */
    drop_engines();
}

TEST_CASE("e32p_j1939tp: BAM size 1785 with 1 packet is rejected (no stale tail)", "[e32p_j1939tp]")
{
    harness_reset();
    make_engines();

    feed_cm(0x20, E32P_J1939_ADDR_GLOBAL, 1785, 1);   /* really needs 255 packets */
    feed_dt(1, 0x44);
    e32p_j1939tp_poll(hB, 100);

    TEST_ASSERT_FALSE(g_rxdone);
    drop_engines();
}

TEST_CASE("e32p_j1939tp: RTS size/packet mismatch gets Conn_Abort and no CTS", "[e32p_j1939tp]")
{
    harness_reset();
    make_engines();

    feed_cm(0x10, B_SA, 100, 5);                       /* 100 bytes needs 15 packets */
    TEST_ASSERT_TRUE(q_has_abort());
    TEST_ASSERT_FALSE(q_has_cm(0x11));                 /* no CTS */
    feed_dt(1, 0x55);
    TEST_ASSERT_FALSE(g_rxdone);

#if CONFIG_E32P_J1939TP_MAX_MSG_BYTES >= 100
    /* The session slot was released: a correct RTS from the same peer is granted. */
    qh = qt = 0;
    feed_cm(0x10, B_SA, 100, 15);
    TEST_ASSERT_TRUE(q_has_cm(0x11));
    TEST_ASSERT_FALSE(q_has_abort());
#endif
    drop_engines();
}

/* Do NOT run against the pre-SEC-06 engine: size 0 with 2 DTs writes out of bounds there. */
TEST_CASE("e32p_j1939tp: sizes under 9 bytes are rejected", "[e32p_j1939tp]")
{
    harness_reset();
    make_engines();

    feed_cm(0x20, E32P_J1939_ADDR_GLOBAL, 0, 2);       /* size 0, count lies */
    feed_dt(1, 0x66);
    feed_dt(2, 0x66);
    TEST_ASSERT_FALSE(g_rxdone);

    feed_cm(0x20, E32P_J1939_ADDR_GLOBAL, 8, 2);       /* count is right, size is below the floor */
    feed_dt(1, 0x77);
    feed_dt(2, 0x77);
    TEST_ASSERT_FALSE(g_rxdone);

    feed_cm(0x10, B_SA, 8, 2);                         /* same on the RTS path */
    TEST_ASSERT_TRUE(q_has_abort());
    TEST_ASSERT_FALSE(q_has_cm(0x11));
    TEST_ASSERT_TRUE(heap_caps_check_integrity_all(true));   /* no heap overrun */
    drop_engines();
}

TEST_CASE("e32p_j1939tp: size boundary 9 bytes: RTS 9/3 rejected, BAM 9/2 accepted", "[e32p_j1939tp]")
{
    harness_reset();
    make_engines();

    feed_cm(0x10, B_SA, 9, 3);                         /* ceil(9/7) = 2, so 3 over-declares */
    TEST_ASSERT_TRUE(q_has_abort());
    TEST_ASSERT_FALSE(q_has_cm(0x11));

    feed_cm(0x20, E32P_J1939_ADDR_GLOBAL, 9, 2);       /* lower boundary, count is right */
    feed_dt(1, 0x88);
    feed_dt(2, 0x88);
    TEST_ASSERT_TRUE(g_rxdone);
    TEST_ASSERT_EQUAL_UINT16(9, g_rxlen);
    drop_engines();
}

#if CONFIG_E32P_J1939TP_MAX_MSG_BYTES >= 1785
TEST_CASE("e32p_j1939tp: largest legal BAM (1785 B, 255 packets) reassembles intact", "[e32p_j1939tp]")
{
    harness_reset();
    make_engines();

    static uint8_t msg[1785];
    for (int i = 0; i < 1785; i++) msg[i] = (uint8_t)(i * 7 + 3);

    feed_cm(0x20, E32P_J1939_ADDR_GLOBAL, 1785, 255);
    for (int seq = 1; seq <= 255; seq++) {
        e32p_j1939_message_t f = { .priority = 7, .pgn = E32P_J1939_PGN_TP_DT,
                                   .src_addr = A_SA, .dst_addr = E32P_J1939_ADDR_GLOBAL, .len = 8 };
        f.data[0] = (uint8_t)seq;
        memcpy(&f.data[1], &msg[(seq - 1) * TEST_DT_PAYLOAD], TEST_DT_PAYLOAD);
        TEST_ESP_OK(e32p_j1939tp_on_frame(hB, &f));
    }

    TEST_ASSERT_TRUE(g_rxdone);
    TEST_ASSERT_EQUAL_UINT16(1785, g_rxlen);
    TEST_ASSERT_EQUAL_UINT32(TEST_PGN, g_rxpgn);
    TEST_ASSERT_EQUAL_HEX8(A_SA, g_rxsrc);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(msg, g_rx, 1785);
    drop_engines();
}
#endif /* CONFIG_E32P_J1939TP_MAX_MSG_BYTES >= 1785 */
