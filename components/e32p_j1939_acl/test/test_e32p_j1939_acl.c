/**
 * @file test_e32p_j1939_acl.c
 * @brief Unity tests for e32p_j1939_acl — fully headless. NAME codec is pure;
 *        the claim engine is exercised by two engines wired through an in-process
 *        frame queue with a virtual clock (no bus).
 */
#include "unity.h"
#include <string.h>
#include "e32p_j1939_acl.h"

static e32p_j1939_acl_handle_t hA, hB;

static struct { e32p_j1939_message_t f; int to; } q[64];
static int qh, qt;

static esp_err_t txA(const e32p_j1939_message_t *m, void *u) { (void)u; q[qt].f = *m; q[qt].to = 1; qt = (qt + 1) & 63; return ESP_OK; }
static esp_err_t txB(const e32p_j1939_message_t *m, void *u) { (void)u; q[qt].f = *m; q[qt].to = 0; qt = (qt + 1) & 63; return ESP_OK; }

static void pump(void)
{
    while (qh != qt) {
        e32p_j1939_message_t f = q[qh].f;
        int to = q[qh].to;
        qh = (qh + 1) & 63;
        e32p_j1939_acl_on_frame(to == 0 ? hA : hB, &f);
    }
}
static void reset_q(void) { qh = qt = 0; }

/* NAME with a given identity; identity sits in the low bits, so (all else equal)
   a smaller identity yields a numerically smaller NAME (higher arbitration
   priority). */
static uint64_t make_name(uint32_t identity, bool arb)
{
    e32p_j1939_name_t n = { .arbitrary_address_capable = arb, .industry_group = 0,
        .vehicle_system_instance = 0, .vehicle_system = 0, .function = 0,
        .function_instance = 0, .ecu_instance = 0, .manufacturer_code = 100,
        .identity_number = identity };
    return e32p_j1939_name_encode(&n);
}

TEST_CASE("e32p_j1939_acl: NAME encode/decode round-trip", "[e32p_j1939_acl]")
{
    e32p_j1939_name_t n = { .arbitrary_address_capable = true, .industry_group = 2,
        .vehicle_system_instance = 3, .vehicle_system = 5, .function = 128,
        .function_instance = 7, .ecu_instance = 1, .manufacturer_code = 1234,
        .identity_number = 0x155AA };
    e32p_j1939_name_t d;
    e32p_j1939_name_decode(e32p_j1939_name_encode(&n), &d);
    TEST_ASSERT_EQUAL(n.arbitrary_address_capable, d.arbitrary_address_capable);
    TEST_ASSERT_EQUAL_UINT8(n.industry_group, d.industry_group);
    TEST_ASSERT_EQUAL_UINT8(n.vehicle_system_instance, d.vehicle_system_instance);
    TEST_ASSERT_EQUAL_UINT8(n.vehicle_system, d.vehicle_system);
    TEST_ASSERT_EQUAL_UINT8(n.function, d.function);
    TEST_ASSERT_EQUAL_UINT8(n.function_instance, d.function_instance);
    TEST_ASSERT_EQUAL_UINT8(n.ecu_instance, d.ecu_instance);
    TEST_ASSERT_EQUAL_UINT16(n.manufacturer_code, d.manufacturer_code);
    TEST_ASSERT_EQUAL_UINT32(n.identity_number, d.identity_number);
}

TEST_CASE("e32p_j1939_acl: single node claims its preferred address", "[e32p_j1939_acl]")
{
    reset_q();
    e32p_j1939_acl_config_t ca = { .name = make_name(1000, true), .preferred_sa = 0x80, .tx = txA };
    TEST_ESP_OK(e32p_j1939_acl_init(&ca, &hA));
    TEST_ESP_OK(e32p_j1939_acl_start(hA));
    for (uint32_t t = 0; t < 400 && !e32p_j1939_acl_is_claimed(hA); t += 25) {
        e32p_j1939_acl_poll(hA, t);   /* no peer -> no contention */
    }
    TEST_ASSERT_TRUE(e32p_j1939_acl_is_claimed(hA));
    TEST_ASSERT_EQUAL_HEX8(0x80, e32p_j1939_acl_current_sa(hA));
    e32p_j1939_acl_deinit(hA); hA = NULL;
}

TEST_CASE("e32p_j1939_acl: lower NAME wins; higher relocates", "[e32p_j1939_acl]")
{
    reset_q();
    e32p_j1939_acl_config_t ca = { .name = make_name(100,  true), .preferred_sa = 0x80, .tx = txA };
    e32p_j1939_acl_config_t cb = { .name = make_name(9000, true), .preferred_sa = 0x80, .tx = txB };
    TEST_ESP_OK(e32p_j1939_acl_init(&ca, &hA));
    TEST_ESP_OK(e32p_j1939_acl_init(&cb, &hB));
    e32p_j1939_acl_start(hA);
    e32p_j1939_acl_start(hB);
    for (uint32_t t = 0; t < 2000; t += 25) {
        e32p_j1939_acl_poll(hA, t);
        e32p_j1939_acl_poll(hB, t);
        pump();
    }
    TEST_ASSERT_TRUE(e32p_j1939_acl_is_claimed(hA));
    TEST_ASSERT_EQUAL_HEX8(0x80, e32p_j1939_acl_current_sa(hA));   /* A keeps it */
    TEST_ASSERT_TRUE(e32p_j1939_acl_is_claimed(hB));
    TEST_ASSERT_NOT_EQUAL(0x80, e32p_j1939_acl_current_sa(hB));    /* B moved */
    e32p_j1939_acl_deinit(hA); hA = NULL;
    e32p_j1939_acl_deinit(hB); hB = NULL;
}

TEST_CASE("e32p_j1939_acl: loser without arbitrary-address goes Cannot-Claim", "[e32p_j1939_acl]")
{
    reset_q();
    /* Arbitrary-address-capable is the NAME's MSB (bit 63), so an arb-capable
       node always has a *higher* NAME than a non-arb one. To make the non-arb
       node (B) the loser, both are non-arb and B has the higher identity. */
    e32p_j1939_acl_config_t ca = { .name = make_name(100,  false), .preferred_sa = 0x80, .tx = txA };
    e32p_j1939_acl_config_t cb = { .name = make_name(9000, false), .preferred_sa = 0x80, .tx = txB };
    TEST_ESP_OK(e32p_j1939_acl_init(&ca, &hA));
    TEST_ESP_OK(e32p_j1939_acl_init(&cb, &hB));
    e32p_j1939_acl_start(hA);
    e32p_j1939_acl_start(hB);
    for (uint32_t t = 0; t < 2000; t += 25) {
        e32p_j1939_acl_poll(hA, t);
        e32p_j1939_acl_poll(hB, t);
        pump();
    }
    TEST_ASSERT_TRUE(e32p_j1939_acl_is_claimed(hA));
    TEST_ASSERT_FALSE(e32p_j1939_acl_is_claimed(hB));
    TEST_ASSERT_EQUAL_HEX8(E32P_J1939_ADDR_NULL, e32p_j1939_acl_current_sa(hB));
    e32p_j1939_acl_deinit(hA); hA = NULL;
    e32p_j1939_acl_deinit(hB); hB = NULL;
}

TEST_CASE("e32p_j1939_acl: answers a Request for Address Claimed", "[e32p_j1939_acl]")
{
    reset_q();
    e32p_j1939_acl_config_t ca = { .name = make_name(1000, true), .preferred_sa = 0x21, .tx = txA };
    TEST_ESP_OK(e32p_j1939_acl_init(&ca, &hA));
    e32p_j1939_acl_start(hA);
    for (uint32_t t = 0; t < 400 && !e32p_j1939_acl_is_claimed(hA); t += 25) {
        e32p_j1939_acl_poll(hA, t);
    }
    TEST_ASSERT_TRUE(e32p_j1939_acl_is_claimed(hA));

    reset_q();
    e32p_j1939_message_t req = { .pgn = E32P_J1939_PGN_REQUEST, .src_addr = 0x30,
        .dst_addr = E32P_J1939_ADDR_GLOBAL, .len = 3,
        .data = { (uint8_t)(E32P_J1939_PGN_ADDR_CLAIMED & 0xFF),
                  (uint8_t)((E32P_J1939_PGN_ADDR_CLAIMED >> 8) & 0xFF),
                  (uint8_t)((E32P_J1939_PGN_ADDR_CLAIMED >> 16) & 0xFF) } };
    TEST_ESP_OK(e32p_j1939_acl_on_frame(hA, &req));
    TEST_ASSERT_NOT_EQUAL(qh, qt);   /* a reply was emitted */
    TEST_ASSERT_EQUAL_UINT32(E32P_J1939_PGN_ADDR_CLAIMED, q[qh].f.pgn);
    TEST_ASSERT_EQUAL_HEX8(0x21, q[qh].f.src_addr);
    e32p_j1939_acl_deinit(hA); hA = NULL;
}

TEST_CASE("e32p_j1939_acl: rejects bad args", "[e32p_j1939_acl]")
{
    e32p_j1939_acl_handle_t h = NULL;
    e32p_j1939_acl_config_t bad = { .name = 1, .preferred_sa = 0x80, .tx = NULL };
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_j1939_acl_init(&bad, &h));
    e32p_j1939_acl_config_t bad2 = { .name = 1, .preferred_sa = 254, .tx = txA };
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_j1939_acl_init(&bad2, &h));
}
