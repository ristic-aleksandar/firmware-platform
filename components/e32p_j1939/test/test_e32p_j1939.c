/**
 * @file test_e32p_j1939.c
 * @brief Unity tests for e32p_j1939. The ID codec is pure (no hardware); the
 *        send/receive round-trip uses TWAI NO_ACK self-reception on one board.
 */
#include "unity.h"
#include "e32p_twai.h"
#include "e32p_j1939.h"

/* --- Pure ID codec ------------------------------------------------------- */

TEST_CASE("e32p_j1939: PDU2 encode matches a known real ID (EEC1)", "[e32p_j1939]")
{
    /* EEC1: PGN 61444 (0xF004, PDU2), priority 3, SA 0 -> classic ID 0x0CF00400. */
    uint32_t id = e32p_j1939_id_encode(3, 61444u, 0x00u, E32P_J1939_ADDR_GLOBAL);
    TEST_ASSERT_EQUAL_HEX32(0x0CF00400u, id);

    uint8_t prio, src, dst; uint32_t pgn;
    e32p_j1939_id_decode(id, &prio, &pgn, &src, &dst);
    TEST_ASSERT_EQUAL_UINT8(3, prio);
    TEST_ASSERT_EQUAL_UINT32(61444u, pgn);
    TEST_ASSERT_EQUAL_HEX8(0x00, src);
    TEST_ASSERT_EQUAL_HEX8(E32P_J1939_ADDR_GLOBAL, dst); /* PDU2 => global */
}

TEST_CASE("e32p_j1939: PDU1 carries destination in PS (TP.CM)", "[e32p_j1939]")
{
    /* TP.CM PGN 60416 (0xEC00, PDU1), prio 7, SA 0x80, DA 0x21 -> 0x1CEC2180. */
    uint32_t id = e32p_j1939_id_encode(7, E32P_J1939_PGN_TP_CM, 0x80u, 0x21u);
    TEST_ASSERT_EQUAL_HEX32(0x1CEC2180u, id);

    uint8_t prio, src, dst; uint32_t pgn;
    e32p_j1939_id_decode(id, &prio, &pgn, &src, &dst);
    TEST_ASSERT_EQUAL_UINT32(60416u, pgn);   /* PS is NOT part of a PDU1 PGN */
    TEST_ASSERT_EQUAL_HEX8(0x80, src);
    TEST_ASSERT_EQUAL_HEX8(0x21, dst);
}

TEST_CASE("e32p_j1939: PDU1/PDU2 boundary at PF=240", "[e32p_j1939]")
{
    TEST_ASSERT_TRUE(e32p_j1939_pgn_is_pdu1(0xEF00u));   /* PF 239 -> PDU1 */
    TEST_ASSERT_FALSE(e32p_j1939_pgn_is_pdu1(0xF000u));  /* PF 240 -> PDU2 */
    TEST_ASSERT_TRUE(e32p_j1939_pgn_is_pdu1(E32P_J1939_PGN_TP_CM));      /* 0xEC */
    TEST_ASSERT_FALSE(e32p_j1939_pgn_is_pdu1(65262u));   /* 0xFEEE -> PDU2 */
}

TEST_CASE("e32p_j1939: encode/decode round-trips both PDU types", "[e32p_j1939]")
{
    /* PDU2 broadcast */
    uint8_t prio, src, dst; uint32_t pgn;
    e32p_j1939_id_decode(e32p_j1939_id_encode(6, 65262u, 0x0Au, 0xFFu),
                         &prio, &pgn, &src, &dst);
    TEST_ASSERT_EQUAL_UINT32(65262u, pgn);
    TEST_ASSERT_EQUAL_HEX8(0x0A, src);

    /* PDU1 dest-specific */
    e32p_j1939_id_decode(e32p_j1939_id_encode(3, 59904u, 0x0Au, 0x33u),
                         &prio, &pgn, &src, &dst);
    TEST_ASSERT_EQUAL_UINT8(3, prio);
    TEST_ASSERT_EQUAL_UINT32(59904u, pgn);
    TEST_ASSERT_EQUAL_HEX8(0x0A, src);
    TEST_ASSERT_EQUAL_HEX8(0x33, dst);
}

TEST_CASE("e32p_j1939: decode tolerates NULL out-pointers", "[e32p_j1939]")
{
    /* Must not crash when the caller only wants some fields. */
    e32p_j1939_id_decode(0x0CF00400u, NULL, NULL, NULL, NULL);
    uint32_t pgn = 0;
    e32p_j1939_id_decode(0x0CF00400u, NULL, &pgn, NULL, NULL);
    TEST_ASSERT_EQUAL_UINT32(61444u, pgn);
}

/* --- On-target integration (transmit path, headless-safe) ----------------
 * NO_ACK queues/transmits without an ACK, but *receiving* the frame back is a
 * physical loopback needing a TX->RX jumper or transceiver (per the IDF
 * twai_self_test README) — so this suite asserts the send path only, matching
 * the e32p_twai test. The full receive round-trip is jumper-gated (test I-23 /
 * demo 17_j1939_send). Codec round-trips above cover encode<->decode headless. */

TEST_CASE("e32p_j1939: send transmits over TWAI (NO_ACK, no wire)", "[e32p_j1939]")
{
    e32p_twai_deinit();  /* clear any state a prior suite left installed */

    e32p_twai_config_t bus = { .tx_gpio = 21, .rx_gpio = 22,
                               .bitrate_bps = 250000, .mode = E32P_TWAI_MODE_NO_ACK };
    TEST_ESP_OK(e32p_twai_init(&bus));
    TEST_ESP_OK(e32p_twai_start());

    e32p_j1939_config_t cfg = { .src_addr = 0x80u, .default_priority = 6u,
                                .self_reception = true };
    TEST_ESP_OK(e32p_j1939_init(&cfg));

    e32p_j1939_message_t tx = { .priority = 0xFFu /* use default */, .pgn = 65262u,
                                .dst_addr = E32P_J1939_ADDR_GLOBAL, .len = 3,
                                .data = { 0xDE, 0xAD, 0xBE } };
    TEST_ESP_OK(e32p_j1939_send(&tx, 100));  /* queues in NO_ACK; RX needs a wire */

    e32p_j1939_deinit();
    e32p_twai_stop();
    e32p_twai_deinit();
}

TEST_CASE("e32p_j1939: rejects bad args", "[e32p_j1939]")
{
    e32p_j1939_config_t cfg = { .src_addr = 254u };  /* out of 0..253 range */
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_j1939_init(&cfg));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_j1939_init(NULL));

    /* send before init -> INVALID_STATE */
    e32p_j1939_deinit();
    e32p_j1939_message_t m = { .pgn = 65262u, .len = 0 };
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, e32p_j1939_send(&m, 10));
}
