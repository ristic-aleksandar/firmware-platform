/**
 * @file test_e32p_uds.c
 * @brief Unity tests for e32p_uds — fully headless, with a virtual clock.
 *
 * Every message the server sends lands in a capture log, and time is whatever the
 * case passes in, so the S3 timeout is checked to the millisecond without waiting.
 * Each case checks the exact bytes a tester would see on the bus.
 *
 * Slice 3.1: the precondition chain, sessions and S3, 0x10, 0x11, 0x22, 0x2E, 0x3E.
 * The two session steps of the chain (0x7E, 0x7F) are covered from slice 3.2 on,
 * when 0x27 gives a service that ISO 14229-1 forbids in the default session.
 */
#include "unity.h"
#include <string.h>
#include "e32p_uds.h"

#define TAG_UDS  "[e32p_uds]"
#define MAX_MSGS 8
#define MAX_LEN  300

/* ---- fixture ---------------------------------------------------------------- */

static uint8_t  g_msg[MAX_MSGS][MAX_LEN];
static uint16_t g_msg_len[MAX_MSGS];
static int      g_msgs;            /* messages sent since the last request      */
static int      g_resets;          /* on_reset calls                            */
static uint8_t  g_reset_type;
static int      g_msgs_at_reset;   /* messages already sent when reset ran      */
static uint8_t  g_config[4];       /* backing store of the writable DID         */

static esp_err_t send(const uint8_t *data, uint16_t len, void *user)
{
    (void)user;
    if (g_msgs < MAX_MSGS) {
        memcpy(g_msg[g_msgs], data, len < MAX_LEN ? len : MAX_LEN);
        g_msg_len[g_msgs] = len;
    }
    g_msgs++;
    return ESP_OK;
}

static void on_reset(uint8_t type, void *user)
{
    (void)user;
    g_resets++;
    g_reset_type = type;
    g_msgs_at_reset = g_msgs;
}

#define DID_VIN    0xF190u
#define DID_SERIAL 0xF18Cu
#define DID_CONFIG 0x0100u   /* writable in extended, no security       */
#define DID_SECRET 0x0101u   /* writable in extended, needs security    */
#define DID_PROG   0x0102u   /* readable only in the programming session */

static const e32p_uds_did_t k_dids[] = {
    { DID_VIN,    17u, E32P_UDS_IN_ANY,         0u,                   false },
    { DID_SERIAL,  8u, E32P_UDS_IN_ANY,         0u,                   false },
    { DID_CONFIG,  4u, E32P_UDS_IN_ANY,         E32P_UDS_IN_EXTENDED, false },
    { DID_SECRET,  2u, E32P_UDS_IN_ANY,         E32P_UDS_IN_EXTENDED, true  },
    { DID_PROG,    2u, E32P_UDS_IN_PROGRAMMING, 0u,                   false },
};

static e32p_uds_nrc_t did_read(uint16_t did, uint8_t *out, uint16_t len, void *user)
{
    (void)user;
    switch (did) {
    case DID_VIN:    memcpy(out, "E32PUDS0000000001", len); break;
    case DID_SERIAL: memcpy(out, "SN000042", len);          break;
    case DID_CONFIG: memcpy(out, g_config, len);            break;
    default:         out[0] = 0xABu;                        break;
    }
    return E32P_UDS_NRC_OK;
}

/* Switches that make a callback answer "still working" (PENDING), slice 3.4. */
static bool g_write_pending;
static bool g_clear_pending;
static bool g_routine_pending;

static e32p_uds_nrc_t did_write(uint16_t did, const uint8_t *data, uint16_t len, void *user)
{
    (void)user;
    if (did == DID_CONFIG) {
        memcpy(g_config, data, len);
    }
    return g_write_pending ? E32P_UDS_NRC_PENDING : E32P_UDS_NRC_OK;
}

/* Security access. The key check is a deliberately trivial XOR: a test fixture,
   not an algorithm anyone should ship — the real one lives in the application,
   with its secret outside the source. */
static const uint8_t k_key_mask[E32P_UDS_SECURITY_LEN] = { 0xA5u, 0x5Au, 0xC3u, 0x3Cu };
static int     g_seeds;      /* seeds handed out by the application        */
static uint8_t g_seed_nrc;   /* non-zero: the application refuses a seed   */

static e32p_uds_nrc_t seed_fn(uint8_t level, uint8_t *seed, uint8_t len, void *user)
{
    (void)user;
    (void)level;
    if (g_seed_nrc != 0u) {
        return (e32p_uds_nrc_t)g_seed_nrc;
    }
    for (uint8_t i = 0; i < len; i++) {
        seed[i] = (uint8_t)(0x10u * (i + 1u) + (uint8_t)g_seeds);   /* fresh every time */
    }
    g_seeds++;
    return E32P_UDS_NRC_OK;
}

static bool key_fn(uint8_t level, const uint8_t *seed, const uint8_t *key, uint8_t len, void *user)
{
    (void)user;
    if (level != 1u) {
        return false;
    }
    for (uint8_t i = 0; i < len; i++) {
        if (key[i] != (uint8_t)(seed[i] ^ k_key_mask[i])) {
            return false;
        }
    }
    return true;
}

/* Security-delay hooks (after-start security delay). By default the fixture says "the last access
   did not fail", so a server starts with no delay and the older cases keep their
   meaning. A case picks another mode for the next make_s3() only. */
enum { SEC_HOOKS_NOT_FAILED = 0, SEC_HOOKS_NONE, SEC_HOOKS_FAILED };
static int  g_cfg_sec_mode;       /* one of SEC_HOOKS_*; used by the next make_s3(), then cleared */
static bool g_sec_load_value;
static int  g_sec_loads;
static int  g_sec_stores;
static bool g_sec_last_stored;
static bool g_sec_store_ok = true;   /* what the store hook reports; reset by make_s3() */

static bool sec_load_fn(void *user)
{
    (void)user;
    g_sec_loads++;
    return g_sec_load_value;
}

static bool sec_store_fn(bool failed, void *user)
{
    (void)user;
    g_sec_stores++;
    g_sec_last_stored = failed;
    return g_sec_store_ok;
}

/* Trouble codes: a small store behind the provider interface. Status bits used:
   testFailed (0x01), pendingDTC (0x04), confirmedDTC (0x08). */
#define DTC_AVAIL 0x0Du

typedef struct {
    uint32_t dtc;
    uint8_t  status;
} dtc_entry_t;

static dtc_entry_t g_dtc[3];
static uint16_t    g_dtc_count;
static uint16_t    g_dtc_flood;       /* non-zero: report this many synthetic codes */
static uint32_t    g_cleared_group;
static uint8_t     g_clear_nrc;       /* non-zero: the clear fails with this NRC    */

static void dtc_store_reset(void)
{
    const dtc_entry_t initial[3] = {
        { 0x013300u, 0x09u },   /* P0133: confirmed, failing now */
        { 0x042000u, 0x04u },   /* P0420: pending                */
        { 0xC07300u, 0x00u },   /* U0073: known, never failed    */
    };
    memcpy(g_dtc, initial, sizeof(initial));
    g_dtc_count = 3u;
    g_dtc_flood = 0u;
    g_cleared_group = 0u;
    g_clear_nrc = 0u;
}

static uint16_t dtc_count(void *user)
{
    (void)user;
    return (g_dtc_flood != 0u) ? g_dtc_flood : g_dtc_count;
}

static bool dtc_at(uint16_t index, uint32_t *dtc, uint8_t *status, void *user)
{
    (void)user;
    if (g_dtc_flood != 0u) {
        if (index >= g_dtc_flood) {
            return false;
        }
        *dtc = 0x010000u + index;
        *status = 0x08u;
        return true;
    }
    if (index >= g_dtc_count) {
        return false;
    }
    *dtc = g_dtc[index].dtc;
    *status = g_dtc[index].status;
    return true;
}

static e32p_uds_nrc_t dtc_clear(uint32_t group, void *user)
{
    (void)user;
    g_cleared_group = group;
    if (g_clear_pending) {
        return E32P_UDS_NRC_PENDING;
    }
    if (g_clear_nrc != 0u) {
        return (e32p_uds_nrc_t)g_clear_nrc;
    }
    g_dtc_count = 0u;
    return E32P_UDS_NRC_OK;
}

static const e32p_uds_dtc_provider_t k_dtc = {
    DTC_AVAIL, E32P_UDS_DTC_FORMAT_ISO15031, dtc_count, dtc_at, dtc_clear,
};

/* Routines. */
#define RID_SELFTEST 0x0201u   /* any session; results only after a start  */
#define RID_EXT_ONLY 0x0202u   /* extended session only                    */
#define RID_ERASE    0xFF00u   /* non-default sessions, needs security     */

static const e32p_uds_routine_t k_routines[] = {
    { RID_SELFTEST, E32P_UDS_IN_ANY,                                false },
    { RID_EXT_ONLY, E32P_UDS_IN_EXTENDED,                           false },
    { RID_ERASE,    E32P_UDS_IN_PROGRAMMING | E32P_UDS_IN_EXTENDED, true  },
};

static bool     g_selftest_started;
static uint8_t  g_routine_nrc;        /* non-zero: the routine refuses with this */
static uint8_t  g_option[8];
static uint16_t g_option_len;

static e32p_uds_nrc_t routine_fn(uint16_t rid, uint8_t control, const uint8_t *option,
                                 uint16_t option_len, uint8_t *status, uint16_t status_cap,
                                 uint16_t *status_len, void *user)
{
    (void)user;
    (void)status_cap;
    if (g_routine_pending) {
        return E32P_UDS_NRC_PENDING;
    }
    if (g_routine_nrc != 0u) {
        return (e32p_uds_nrc_t)g_routine_nrc;
    }
    g_option_len = option_len;
    memcpy(g_option, option, option_len < sizeof(g_option) ? option_len : sizeof(g_option));
    if (rid == RID_SELFTEST && control == 0x03u) {
        if (!g_selftest_started) {
            return E32P_UDS_NRC_SEQUENCE_ERROR;   /* no results before a start */
        }
        status[0] = 0x00u;                        /* passed */
        status[1] = 0x42u;                        /* its result */
        *status_len = 2u;
        return E32P_UDS_NRC_OK;
    }
    if (rid == RID_SELFTEST) {
        g_selftest_started = true;
    }
    status[0] = 0x00u;                            /* started */
    *status_len = 1u;
    return E32P_UDS_NRC_OK;
}

/* Download: a RAM sink that keeps the image and validates it by CRC-32, the way a
   real sink validates an image before marking it bootable. */
#define SINK_CAP 4096u

static uint8_t  g_image[SINK_CAP];
static uint32_t g_image_len;
static int      g_sink_begins, g_sink_writes, g_sink_ends, g_sink_aborts;
static bool     g_sink_begin_pending, g_sink_write_pending, g_sink_end_pending;
static uint8_t  g_sink_write_nrc;     /* non-zero: the write fails with this NRC   */
static uint32_t g_sink_good_crc;      /* end() accepts only an image with this CRC */
static uint32_t g_cfg_pending_max_ms; /* used by the next make_s3(), then cleared  */

static uint32_t crc32_of(const uint8_t *d, uint32_t n)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < n; i++) {
        crc ^= d[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

static e32p_uds_nrc_t sink_begin(uint32_t size, void *user)
{
    (void)size;
    (void)user;
    g_sink_begins++;
    g_image_len = 0u;
    return g_sink_begin_pending ? E32P_UDS_NRC_PENDING : E32P_UDS_NRC_OK;
}

static e32p_uds_nrc_t sink_write(const uint8_t *data, uint16_t len, void *user)
{
    (void)user;
    g_sink_writes++;
    if (g_sink_write_nrc != 0u) {
        return (e32p_uds_nrc_t)g_sink_write_nrc;
    }
    if (g_image_len + len <= SINK_CAP) {
        memcpy(&g_image[g_image_len], data, len);
        g_image_len += len;
    }
    return g_sink_write_pending ? E32P_UDS_NRC_PENDING : E32P_UDS_NRC_OK;
}

static e32p_uds_nrc_t sink_end(void *user)
{
    (void)user;
    g_sink_ends++;
    if (g_sink_end_pending) {
        return E32P_UDS_NRC_PENDING;
    }
    return (crc32_of(g_image, g_image_len) == g_sink_good_crc) ? E32P_UDS_NRC_OK
                                                                : E32P_UDS_NRC_PROGRAMMING_FAILURE;
}

static void sink_abort(void *user)
{
    (void)user;
    g_sink_aborts++;
}

static const e32p_uds_sink_t k_sink = { SINK_CAP, sink_begin, sink_write, sink_end, sink_abort };

/** @brief A fresh server with every service so far configured. */
static void make_s3(e32p_uds_handle_t *out, uint16_t s3_ms)
{
    g_write_pending = false;
    g_clear_pending = false;
    g_routine_pending = false;
    g_image_len = 0u;
    g_sink_begins = g_sink_writes = g_sink_ends = g_sink_aborts = 0;
    g_sink_begin_pending = g_sink_write_pending = g_sink_end_pending = false;
    g_sink_write_nrc = 0u;
    g_sink_good_crc = 0u;
    dtc_store_reset();
    g_selftest_started = false;
    g_routine_nrc = 0u;
    g_option_len = 0u;
    static const uint8_t initial[4] = { 0x11u, 0x22u, 0x33u, 0x44u };
    memcpy(g_config, initial, sizeof(g_config));
    g_msgs = 0;
    g_resets = 0;
    g_reset_type = 0u;
    g_msgs_at_reset = -1;
    g_seeds = 0;
    g_seed_nrc = 0u;
    const int sec_mode = g_cfg_sec_mode;
    g_cfg_sec_mode = SEC_HOOKS_NOT_FAILED;
    g_sec_load_value = (sec_mode == SEC_HOOKS_FAILED);
    g_sec_loads = g_sec_stores = 0;
    g_sec_last_stored = false;
    g_sec_store_ok = true;
    e32p_uds_config_t cfg = {
        .send = send,
        .dids = k_dids,
        .did_count = (uint8_t)(sizeof(k_dids) / sizeof(k_dids[0])),
        .on_did_read = did_read,
        .on_did_write = did_write,
        .on_seed = seed_fn,
        .on_key = key_fn,
        .on_sec_load = (sec_mode == SEC_HOOKS_NONE) ? NULL : sec_load_fn,
        .on_sec_store = (sec_mode == SEC_HOOKS_NONE) ? NULL : sec_store_fn,
        .dtc = &k_dtc,
        .routines = k_routines,
        .routine_count = (uint8_t)(sizeof(k_routines) / sizeof(k_routines[0])),
        .on_routine = routine_fn,
        .on_reset = on_reset,
        .sink = &k_sink,
        .s3_ms = s3_ms,
        .pending_max_ms = g_cfg_pending_max_ms,
    };
    g_cfg_pending_max_ms = 0u;
    TEST_ASSERT_EQUAL(ESP_OK, e32p_uds_init(&cfg, out));
}

static void make(e32p_uds_handle_t *out)
{
    make_s3(out, 0u);   /* 0: the ISO default, 5 s */
}

/* A request literal: REQ(0x10, 0x03) expands to the bytes and their count. */
#define REQ(...) (const uint8_t[]){ __VA_ARGS__ }, (uint16_t)sizeof((const uint8_t[]){ __VA_ARGS__ })

static void ask(e32p_uds_handle_t h, const uint8_t *req, uint16_t len, uint32_t now_ms)
{
    g_msgs = 0;
    TEST_ASSERT_EQUAL(ESP_OK, e32p_uds_request(h, E32P_UDS_ADDR_PHYSICAL, req, len, now_ms));
}

static void ask_func(e32p_uds_handle_t h, const uint8_t *req, uint16_t len, uint32_t now_ms)
{
    g_msgs = 0;
    TEST_ASSERT_EQUAL(ESP_OK, e32p_uds_request(h, E32P_UDS_ADDR_FUNCTIONAL, req, len, now_ms));
}

/** @brief Exactly one message went out, and it is @p want. */
static void expect(const uint8_t *want, uint16_t len)
{
    TEST_ASSERT_EQUAL_INT(1, g_msgs);
    TEST_ASSERT_EQUAL_UINT16(len, g_msg_len[0]);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, g_msg[0], len);
}

static void expect_nrc(uint8_t sid, uint8_t nrc)
{
    const uint8_t want[3] = { 0x7Fu, sid, nrc };
    expect(want, sizeof(want));
}

static void expect_silence(void)
{
    TEST_ASSERT_EQUAL_INT(0, g_msgs);
}

/* ---- 0x10 and the precondition chain ---------------------------------------- */

TEST_CASE("0x10 enters each session and reports P2 and P2*", TAG_UDS)
{
    e32p_uds_handle_t h;
    make(&h);
    ask(h, REQ(0x10, 0x03), 0);
    const uint8_t want[] = { 0x50, 0x03, 0x00, 0x32, 0x01, 0xF4 };   /* 50 ms, 500 x 10 ms */
    expect(want, sizeof(want));
    TEST_ASSERT_EQUAL(E32P_UDS_SESSION_EXTENDED, e32p_uds_session(h));
    ask(h, REQ(0x10, 0x02), 10);
    TEST_ASSERT_EQUAL(E32P_UDS_SESSION_PROGRAMMING, e32p_uds_session(h));
    ask(h, REQ(0x10, 0x01), 20);
    TEST_ASSERT_EQUAL(E32P_UDS_SESSION_DEFAULT, e32p_uds_session(h));
    e32p_uds_deinit(h);
}

TEST_CASE("0x10 reports the timing the server was configured with", TAG_UDS)
{
    g_msgs = 0;
    e32p_uds_config_t cfg = { .send = send, .p2_ms = 25u, .p2_star_ms = 2000u };
    e32p_uds_handle_t h;
    TEST_ASSERT_EQUAL(ESP_OK, e32p_uds_init(&cfg, &h));
    ask(h, REQ(0x10, 0x03), 0);
    const uint8_t want[] = { 0x50, 0x03, 0x00, 0x19, 0x00, 0xC8 };   /* 25 ms, 200 x 10 ms */
    expect(want, sizeof(want));
    e32p_uds_deinit(h);
}

TEST_CASE("chain: an unknown service answers 0x11 before its length is checked", TAG_UDS)
{
    e32p_uds_handle_t h;
    make(&h);
    ask(h, REQ(0x99), 0);
    expect_nrc(0x99, 0x11);
    e32p_uds_deinit(h);
}

TEST_CASE("chain: the minimum length is checked before the sub-function", TAG_UDS)
{
    e32p_uds_handle_t h;
    make(&h);
    ask(h, REQ(0x10), 0);
    expect_nrc(0x10, 0x13);
    e32p_uds_deinit(h);
}

TEST_CASE("0x10: unknown sub-function 0x12, wrong total length 0x13", TAG_UDS)
{
    e32p_uds_handle_t h;
    make(&h);
    ask(h, REQ(0x10, 0x05), 0);
    expect_nrc(0x10, 0x12);
    ask(h, REQ(0x10, 0x03, 0x00), 1);
    expect_nrc(0x10, 0x13);
    TEST_ASSERT_EQUAL(E32P_UDS_SESSION_DEFAULT, e32p_uds_session(h));
    e32p_uds_deinit(h);
}

TEST_CASE("suppressPosRsp hides a positive response, never a negative one", TAG_UDS)
{
    e32p_uds_handle_t h;
    make(&h);
    ask(h, REQ(0x10, 0x83), 0);
    expect_silence();
    TEST_ASSERT_EQUAL(E32P_UDS_SESSION_EXTENDED, e32p_uds_session(h));
    ask(h, REQ(0x10, 0x85), 1);
    expect_nrc(0x10, 0x12);
    e32p_uds_deinit(h);
}

/* ---- S3 --------------------------------------------------------------------- */

TEST_CASE("S3: a non-default session falls back after 5 s of silence, quietly", TAG_UDS)
{
    e32p_uds_handle_t h;
    make(&h);
    ask(h, REQ(0x10, 0x03), 1000);
    g_msgs = 0;
    TEST_ASSERT_EQUAL(ESP_OK, e32p_uds_poll(h, 5999));
    TEST_ASSERT_EQUAL(E32P_UDS_SESSION_EXTENDED, e32p_uds_session(h));
    TEST_ASSERT_EQUAL(ESP_OK, e32p_uds_poll(h, 6000));
    TEST_ASSERT_EQUAL(E32P_UDS_SESSION_DEFAULT, e32p_uds_session(h));
    expect_silence();
    e32p_uds_deinit(h);
}

TEST_CASE("S3: any request restarts the timer, even one answered negatively", TAG_UDS)
{
    e32p_uds_handle_t h;
    make(&h);
    ask(h, REQ(0x10, 0x03), 0);
    ask(h, REQ(0x99), 4000);
    expect_nrc(0x99, 0x11);
    TEST_ASSERT_EQUAL(ESP_OK, e32p_uds_poll(h, 8999));
    TEST_ASSERT_EQUAL(E32P_UDS_SESSION_EXTENDED, e32p_uds_session(h));
    TEST_ASSERT_EQUAL(ESP_OK, e32p_uds_poll(h, 9000));
    TEST_ASSERT_EQUAL(E32P_UDS_SESSION_DEFAULT, e32p_uds_session(h));
    e32p_uds_deinit(h);
}

TEST_CASE("S3: a request after the timeout is answered in the default session", TAG_UDS)
{
    e32p_uds_handle_t h;
    make(&h);
    ask(h, REQ(0x10, 0x03), 0);
    ask(h, REQ(0x2E, 0x01, 0x00, 0xAA, 0xBB, 0xCC, 0xDD), 4000);   /* still extended */
    expect(REQ(0x6E, 0x01, 0x00));
    /* No poll() in between: the request itself must notice the session has expired. */
    ask(h, REQ(0x2E, 0x01, 0x00, 0xAA, 0xBB, 0xCC, 0xDD), 9000);
    expect_nrc(0x2E, 0x31);
    e32p_uds_deinit(h);
}

TEST_CASE("S3 never runs in the default session", TAG_UDS)
{
    e32p_uds_handle_t h;
    make(&h);
    g_msgs = 0;
    TEST_ASSERT_EQUAL(ESP_OK, e32p_uds_poll(h, 1000000));
    TEST_ASSERT_EQUAL(E32P_UDS_SESSION_DEFAULT, e32p_uds_session(h));
    expect_silence();
    e32p_uds_deinit(h);
}

/* ---- 0x3E ------------------------------------------------------------------- */

TEST_CASE("0x3E answers 7E 00; 3E 80 answers nothing but keeps the session", TAG_UDS)
{
    e32p_uds_handle_t h;
    make(&h);
    ask(h, REQ(0x10, 0x03), 0);
    ask(h, REQ(0x3E, 0x80), 4000);
    expect_silence();
    TEST_ASSERT_EQUAL(ESP_OK, e32p_uds_poll(h, 8999));
    TEST_ASSERT_EQUAL(E32P_UDS_SESSION_EXTENDED, e32p_uds_session(h));
    ask(h, REQ(0x3E, 0x00), 8999);
    expect(REQ(0x7E, 0x00));
    e32p_uds_deinit(h);
}

TEST_CASE("0x3E with any other sub-function answers 0x12", TAG_UDS)
{
    e32p_uds_handle_t h;
    make(&h);
    ask(h, REQ(0x3E, 0x01), 0);
    expect_nrc(0x3E, 0x12);
    e32p_uds_deinit(h);
}

/* ---- functional addressing (D-18) ------------------------------------------- */

TEST_CASE("functional: 0x11, 0x12 and 0x31 stay silent, other answers go out", TAG_UDS)
{
    e32p_uds_handle_t h;
    make(&h);
    ask_func(h, REQ(0x99), 0);
    expect_silence();                          /* 0x11 */
    ask_func(h, REQ(0x10, 0x05), 1);
    expect_silence();                          /* 0x12 */
    ask_func(h, REQ(0x22, 0xFF, 0xFF), 2);
    expect_silence();                          /* 0x31 */
    ask_func(h, REQ(0x10), 3);
    expect_nrc(0x10, 0x13);                    /* not suppressed */
    ask_func(h, REQ(0x3E, 0x00), 4);
    expect(REQ(0x7E, 0x00));                   /* positive answers go out */
    e32p_uds_deinit(h);
}

/* ---- 0x22 ------------------------------------------------------------------- */

TEST_CASE("0x22 reads one DID", TAG_UDS)
{
    e32p_uds_handle_t h;
    make(&h);
    ask(h, REQ(0x22, 0xF1, 0x90), 0);
    uint8_t want[3 + 17] = { 0x62, 0xF1, 0x90 };
    memcpy(&want[3], "E32PUDS0000000001", 17);
    expect(want, sizeof(want));
    e32p_uds_deinit(h);
}

TEST_CASE("0x22 reads several DIDs and skips the ones it does not serve", TAG_UDS)
{
    e32p_uds_handle_t h;
    make(&h);
    ask(h, REQ(0x22, 0xF1, 0x90, 0xFF, 0xFF, 0xF1, 0x8C), 0);
    uint8_t want[1 + 2 + 17 + 2 + 8] = { 0x62, 0xF1, 0x90 };
    memcpy(&want[3], "E32PUDS0000000001", 17);
    want[20] = 0xF1;
    want[21] = 0x8C;
    memcpy(&want[22], "SN000042", 8);
    expect(want, sizeof(want));
    e32p_uds_deinit(h);
}

TEST_CASE("0x22: no DID served 0x31, malformed request 0x13", TAG_UDS)
{
    e32p_uds_handle_t h;
    make(&h);
    ask(h, REQ(0x22, 0xFF, 0xFF), 0);
    expect_nrc(0x22, 0x31);
    ask(h, REQ(0x22, 0xF1), 1);
    expect_nrc(0x22, 0x13);
    ask(h, REQ(0x22), 2);
    expect_nrc(0x22, 0x13);
    e32p_uds_deinit(h);
}

TEST_CASE("0x22 honours the session a DID can be read in", TAG_UDS)
{
    e32p_uds_handle_t h;
    make(&h);
    ask(h, REQ(0x22, 0x01, 0x02), 0);
    expect_nrc(0x22, 0x31);
    ask(h, REQ(0x10, 0x02), 1);
    ask(h, REQ(0x22, 0x01, 0x02), 2);
    expect(REQ(0x62, 0x01, 0x02, 0xAB, 0x00));
    e32p_uds_deinit(h);
}

TEST_CASE("0x22 answers 0x14 when the response would not fit", TAG_UDS)
{
    e32p_uds_handle_t h;
    make(&h);
    uint8_t req[1 + 2 * 14];
    req[0] = 0x22;
    for (int i = 0; i < 14; i++) {   /* 14 x (2 + 17) + 1 = 267 bytes > 256 */
        req[1 + 2 * i] = 0xF1;
        req[2 + 2 * i] = 0x90;
    }
    ask(h, req, sizeof(req), 0);
    expect_nrc(0x22, 0x14);
    e32p_uds_deinit(h);
}

/* ---- 0x2E ------------------------------------------------------------------- */

TEST_CASE("0x2E writes a DID only in the session it allows, and it reads back", TAG_UDS)
{
    e32p_uds_handle_t h;
    make(&h);
    ask(h, REQ(0x2E, 0x01, 0x00, 0xAA, 0xBB, 0xCC, 0xDD), 0);
    expect_nrc(0x2E, 0x31);
    ask(h, REQ(0x10, 0x03), 1);
    ask(h, REQ(0x2E, 0x01, 0x00, 0xAA, 0xBB, 0xCC, 0xDD), 2);
    expect(REQ(0x6E, 0x01, 0x00));
    ask(h, REQ(0x22, 0x01, 0x00), 3);
    expect(REQ(0x62, 0x01, 0x00, 0xAA, 0xBB, 0xCC, 0xDD));
    e32p_uds_deinit(h);
}

TEST_CASE("0x2E checks security before the data length", TAG_UDS)
{
    e32p_uds_handle_t h;
    make(&h);
    ask(h, REQ(0x10, 0x03), 0);
    ask(h, REQ(0x2E, 0x01, 0x01, 0xAA), 1);          /* wrong length, and locked */
    expect_nrc(0x2E, 0x33);
    ask(h, REQ(0x2E, 0x01, 0x01, 0xAA, 0xBB), 2);    /* right length, still locked */
    expect_nrc(0x2E, 0x33);
    e32p_uds_deinit(h);
}

TEST_CASE("0x2E: wrong data length 0x13; read-only or unknown DID 0x31", TAG_UDS)
{
    e32p_uds_handle_t h;
    make(&h);
    ask(h, REQ(0x10, 0x03), 0);
    ask(h, REQ(0x2E, 0x01, 0x00, 0xAA), 1);
    expect_nrc(0x2E, 0x13);
    ask(h, REQ(0x2E, 0x01, 0x00), 2);
    expect_nrc(0x2E, 0x13);
    ask(h, REQ(0x2E, 0xF1, 0x90, 0x41), 3);
    expect_nrc(0x2E, 0x31);
    ask(h, REQ(0x2E, 0xFF, 0xFF, 0x41), 4);
    expect_nrc(0x2E, 0x31);
    e32p_uds_deinit(h);
}

/* ---- 0x11 ------------------------------------------------------------------- */

TEST_CASE("0x11 answers 51 01 first, then resets to the default session", TAG_UDS)
{
    e32p_uds_handle_t h;
    make(&h);
    ask(h, REQ(0x10, 0x03), 0);
    ask(h, REQ(0x11, 0x01), 1);
    expect(REQ(0x51, 0x01));
    TEST_ASSERT_EQUAL_INT(1, g_resets);
    TEST_ASSERT_EQUAL_HEX8(0x01, g_reset_type);
    TEST_ASSERT_EQUAL_INT(1, g_msgs_at_reset);      /* the response left before the reset */
    TEST_ASSERT_EQUAL(E32P_UDS_SESSION_DEFAULT, e32p_uds_session(h));
    e32p_uds_deinit(h);
}

TEST_CASE("0x11 81 resets without answering", TAG_UDS)
{
    e32p_uds_handle_t h;
    make(&h);
    ask(h, REQ(0x11, 0x81), 0);
    expect_silence();
    TEST_ASSERT_EQUAL_INT(1, g_resets);
    e32p_uds_deinit(h);
}

TEST_CASE("0x11 with an unsupported reset type answers 0x12 and does not reset", TAG_UDS)
{
    e32p_uds_handle_t h;
    make(&h);
    ask(h, REQ(0x11, 0x02), 0);
    expect_nrc(0x11, 0x12);
    TEST_ASSERT_EQUAL_INT(0, g_resets);
    e32p_uds_deinit(h);
}

/* ---- configuration ---------------------------------------------------------- */

TEST_CASE("a service whose callback is not configured is not supported", TAG_UDS)
{
    g_msgs = 0;
    e32p_uds_config_t cfg = { .send = send };
    e32p_uds_handle_t h;
    TEST_ASSERT_EQUAL(ESP_OK, e32p_uds_init(&cfg, &h));
    ask(h, REQ(0x22, 0xF1, 0x90), 0);
    expect_nrc(0x22, 0x11);
    ask(h, REQ(0x2E, 0x01, 0x00, 0xAA), 1);
    expect_nrc(0x2E, 0x11);
    ask(h, REQ(0x11, 0x01), 2);
    expect_nrc(0x11, 0x11);
    ask(h, REQ(0x3E, 0x00), 3);                       /* needs nothing */
    expect(REQ(0x7E, 0x00));
    e32p_uds_deinit(h);
}

TEST_CASE("init refuses a DID that cannot fit in one response", TAG_UDS)
{
    e32p_uds_handle_t h;
    e32p_uds_did_t did = { 0x0200u, 0u, E32P_UDS_IN_ANY, 0u, false };
    e32p_uds_config_t cfg = { .send = send, .dids = &did, .did_count = 1u, .on_did_read = did_read };
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_uds_init(&cfg, &h));
    did.len = 254u;                                   /* 1 + 2 + 254 > 256 */
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_uds_init(&cfg, &h));
    did.len = 253u;
    TEST_ASSERT_EQUAL(ESP_OK, e32p_uds_init(&cfg, &h));
    e32p_uds_deinit(h);
}

TEST_CASE("init wants both security-flag hooks or neither", TAG_UDS)
{
    e32p_uds_handle_t h;
    e32p_uds_config_t cfg = { .send = send, .on_sec_load = sec_load_fn };
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_uds_init(&cfg, &h));   /* load only */
    cfg.on_sec_load = NULL;
    cfg.on_sec_store = sec_store_fn;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_uds_init(&cfg, &h));   /* store only */
    cfg.on_sec_load = sec_load_fn;
    TEST_ASSERT_EQUAL(ESP_OK, e32p_uds_init(&cfg, &h));                /* both */
    e32p_uds_deinit(h);
    cfg.on_sec_load = NULL;
    cfg.on_sec_store = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, e32p_uds_init(&cfg, &h));                /* neither */
    e32p_uds_deinit(h);
}

/* ---- 0x27 security access (slice 3.2) --------------------------------------- */

/** @brief Ask for a seed; it must be a normal 67 01 answer. */
static void get_seed(e32p_uds_handle_t h, uint32_t now_ms, uint8_t seed[E32P_UDS_SECURITY_LEN])
{
    ask(h, REQ(0x27, 0x01), now_ms);
    TEST_ASSERT_EQUAL_INT(1, g_msgs);
    TEST_ASSERT_EQUAL_UINT16(2u + E32P_UDS_SECURITY_LEN, g_msg_len[0]);
    TEST_ASSERT_EQUAL_HEX8(0x67, g_msg[0][0]);
    TEST_ASSERT_EQUAL_HEX8(0x01, g_msg[0][1]);
    memcpy(seed, &g_msg[0][2], E32P_UDS_SECURITY_LEN);
}

/** @brief Send the key for @p seed — the right one, or a wrong one. */
static void send_key(e32p_uds_handle_t h, const uint8_t seed[E32P_UDS_SECURITY_LEN], bool right,
                     uint32_t now_ms)
{
    uint8_t req[2 + E32P_UDS_SECURITY_LEN] = { 0x27, 0x02 };
    for (int i = 0; i < (int)E32P_UDS_SECURITY_LEN; i++) {
        req[2 + i] = (uint8_t)(seed[i] ^ k_key_mask[i] ^ (right ? 0x00u : 0xFFu));
    }
    ask(h, req, sizeof(req), now_ms);
}

static void unlock(e32p_uds_handle_t h, uint32_t now_ms)
{
    uint8_t seed[E32P_UDS_SECURITY_LEN];
    get_seed(h, now_ms, seed);
    send_key(h, seed, true, now_ms);
    expect(REQ(0x67, 0x02));
}

/** @brief Three wrong keys in a row, each with a fresh seed: 0x35, 0x35, 0x36. */
static void lock_out(e32p_uds_handle_t h, uint32_t first_ms)
{
    uint8_t seed[E32P_UDS_SECURITY_LEN];
    for (uint32_t i = 0; i < E32P_UDS_SECURITY_MAX_ATTEMPTS; i++) {
        get_seed(h, first_ms + i, seed);
        send_key(h, seed, false, first_ms + i);
        expect_nrc(0x27, (i + 1u < E32P_UDS_SECURITY_MAX_ATTEMPTS) ? 0x35 : 0x36);
    }
}

TEST_CASE("chain: 0x27 in the default session is 0x7F, before its length is checked", TAG_UDS)
{
    e32p_uds_handle_t h;
    make(&h);
    ask(h, REQ(0x27, 0x01), 0);
    expect_nrc(0x27, 0x7F);
    ask(h, REQ(0x27), 1);                /* too short too: the session step comes first */
    expect_nrc(0x27, 0x7F);
    ask_func(h, REQ(0x27, 0x01), 2);
    expect_silence();                    /* 0x7F is suppressed for broadcasts */
    e32p_uds_deinit(h);
}

TEST_CASE("0x27: a level this server does not have answers 0x12", TAG_UDS)
{
    e32p_uds_handle_t h;
    make(&h);
    ask(h, REQ(0x10, 0x03), 0);
    ask(h, REQ(0x27, 0x03), 1);
    expect_nrc(0x27, 0x12);
    ask(h, REQ(0x27, 0x00), 2);
    expect_nrc(0x27, 0x12);
    e32p_uds_deinit(h);
}

TEST_CASE("0x27: seed then the right key unlocks, and a secured DID can be written", TAG_UDS)
{
    e32p_uds_handle_t h;
    make(&h);
    ask(h, REQ(0x10, 0x03), 0);
    uint8_t seed[E32P_UDS_SECURITY_LEN];
    get_seed(h, 1, seed);
    TEST_ASSERT_EQUAL_HEX8(0x10, seed[0]);           /* it came from the application */
    send_key(h, seed, true, 2);
    expect(REQ(0x67, 0x02));
    ask(h, REQ(0x2E, 0x01, 0x01, 0xAA, 0xBB), 3);
    expect(REQ(0x6E, 0x01, 0x01));
    e32p_uds_deinit(h);
}

TEST_CASE("0x27: an unlocked server answers a seed request with a zero seed", TAG_UDS)
{
    e32p_uds_handle_t h;
    make(&h);
    ask(h, REQ(0x10, 0x03), 0);
    unlock(h, 1);
    const int seeds_before = g_seeds;
    ask(h, REQ(0x27, 0x01), 2);
    expect(REQ(0x67, 0x01, 0x00, 0x00, 0x00, 0x00));
    TEST_ASSERT_EQUAL_INT(seeds_before, g_seeds);    /* the application was not asked */
    ask(h, REQ(0x27, 0x02, 0x00, 0x00, 0x00, 0x00), 3);
    expect_nrc(0x27, 0x24);                          /* a zero seed expects no key */
    e32p_uds_deinit(h);
}

TEST_CASE("0x27: a key needs a seed, and a seed answers one key only", TAG_UDS)
{
    e32p_uds_handle_t h;
    make(&h);
    ask(h, REQ(0x10, 0x03), 0);
    const uint8_t never_issued[E32P_UDS_SECURITY_LEN] = { 1, 2, 3, 4 };
    send_key(h, never_issued, true, 1);
    expect_nrc(0x27, 0x24);
    uint8_t seed[E32P_UDS_SECURITY_LEN];
    get_seed(h, 2, seed);
    send_key(h, seed, false, 3);
    expect_nrc(0x27, 0x35);
    send_key(h, seed, true, 4);                      /* the right key, but the seed is spent */
    expect_nrc(0x27, 0x24);
    e32p_uds_deinit(h);
}

TEST_CASE("0x27: the third wrong key is 0x36, then seeds are 0x37 for 10 s", TAG_UDS)
{
    e32p_uds_handle_t h;
    make_s3(&h, 60000u);                             /* keep the session through the wait */
    ask(h, REQ(0x10, 0x03), 0);
    lock_out(h, 1);                                  /* 0x36 at t = 3 */
    ask(h, REQ(0x27, 0x01), 3u + E32P_UDS_SECURITY_DELAY_MS - 1u);
    expect_nrc(0x27, 0x37);
    unlock(h, 3u + E32P_UDS_SECURITY_DELAY_MS);      /* over: seeds and keys work again */
    e32p_uds_deinit(h);
}

TEST_CASE("0x27: the lockout survives a session change", TAG_UDS)
{
    e32p_uds_handle_t h;
    make_s3(&h, 60000u);
    ask(h, REQ(0x10, 0x03), 0);
    lock_out(h, 1);
    ask(h, REQ(0x10, 0x01), 4);
    ask(h, REQ(0x10, 0x03), 5);
    ask(h, REQ(0x27, 0x01), 6);
    expect_nrc(0x27, 0x37);                          /* switching sessions does not reset it */
    e32p_uds_deinit(h);
}

TEST_CASE("0x27: the wrong-key count survives a session change", TAG_UDS)
{
    e32p_uds_handle_t h;
    make(&h);
    ask(h, REQ(0x10, 0x03), 0);
    uint8_t seed[E32P_UDS_SECURITY_LEN];
    for (uint32_t i = 0; i < E32P_UDS_SECURITY_MAX_ATTEMPTS - 1u; i++) {
        get_seed(h, 1 + i, seed);
        send_key(h, seed, false, 1 + i);
        expect_nrc(0x27, 0x35);
    }
    ask(h, REQ(0x10, 0x03), 10);
    get_seed(h, 11, seed);
    send_key(h, seed, false, 12);
    expect_nrc(0x27, 0x36);                          /* still the third in a row */
    e32p_uds_deinit(h);
}

TEST_CASE("0x27: the right key clears the wrong-key count", TAG_UDS)
{
    e32p_uds_handle_t h;
    make(&h);
    ask(h, REQ(0x10, 0x03), 0);
    uint8_t seed[E32P_UDS_SECURITY_LEN];
    for (uint32_t i = 0; i < E32P_UDS_SECURITY_MAX_ATTEMPTS - 1u; i++) {
        get_seed(h, 1 + i, seed);
        send_key(h, seed, false, 1 + i);
        expect_nrc(0x27, 0x35);
    }
    unlock(h, 10);
    ask(h, REQ(0x10, 0x03), 11);                     /* lock again */
    get_seed(h, 12, seed);
    send_key(h, seed, false, 13);
    expect_nrc(0x27, 0x35);                          /* counting from one again, not 0x36 */
    e32p_uds_deinit(h);
}

TEST_CASE("0x27: any session change locks again, into the same session too", TAG_UDS)
{
    e32p_uds_handle_t h;
    make(&h);
    ask(h, REQ(0x10, 0x03), 0);
    unlock(h, 1);
    ask(h, REQ(0x10, 0x03), 2);
    ask(h, REQ(0x2E, 0x01, 0x01, 0xAA, 0xBB), 3);
    expect_nrc(0x2E, 0x33);
    uint8_t seed[E32P_UDS_SECURITY_LEN];
    get_seed(h, 4, seed);
    TEST_ASSERT_NOT_EQUAL(0, seed[0] | seed[1] | seed[2] | seed[3]);   /* locked: a real seed */
    e32p_uds_deinit(h);
}

TEST_CASE("0x27: the S3 timeout and an ECU reset lock again", TAG_UDS)
{
    e32p_uds_handle_t h;
    make(&h);
    ask(h, REQ(0x10, 0x03), 0);
    unlock(h, 0);
    TEST_ASSERT_EQUAL(ESP_OK, e32p_uds_poll(h, 5000));
    ask(h, REQ(0x10, 0x03), 5001);
    ask(h, REQ(0x2E, 0x01, 0x01, 0xAA, 0xBB), 5002);
    expect_nrc(0x2E, 0x33);
    unlock(h, 5003);
    ask(h, REQ(0x11, 0x01), 5004);
    ask(h, REQ(0x10, 0x03), 5005);
    ask(h, REQ(0x2E, 0x01, 0x01, 0xAA, 0xBB), 5006);
    expect_nrc(0x2E, 0x33);
    e32p_uds_deinit(h);
}

TEST_CASE("0x27: wrong lengths are 0x13, and a malformed key does not spend the seed", TAG_UDS)
{
    e32p_uds_handle_t h;
    make(&h);
    ask(h, REQ(0x10, 0x03), 0);
    ask(h, REQ(0x27, 0x01, 0x00), 1);
    expect_nrc(0x27, 0x13);
    uint8_t seed[E32P_UDS_SECURITY_LEN];
    get_seed(h, 2, seed);
    ask(h, REQ(0x27, 0x02, 0x00, 0x00, 0x00), 3);   /* key one byte short */
    expect_nrc(0x27, 0x13);
    send_key(h, seed, true, 4);                      /* the seed still stands */
    expect(REQ(0x67, 0x02));
    e32p_uds_deinit(h);
}

TEST_CASE("0x27: a seed the application refuses is answered with its NRC", TAG_UDS)
{
    e32p_uds_handle_t h;
    make(&h);
    ask(h, REQ(0x10, 0x03), 0);
    g_seed_nrc = 0x22;
    ask(h, REQ(0x27, 0x01), 1);
    expect_nrc(0x27, 0x22);
    g_seed_nrc = 0u;
    e32p_uds_deinit(h);
}

TEST_CASE("0x27 without seed and key callbacks is not supported, in any session", TAG_UDS)
{
    g_msgs = 0;
    e32p_uds_config_t cfg = { .send = send };
    e32p_uds_handle_t h;
    TEST_ASSERT_EQUAL(ESP_OK, e32p_uds_init(&cfg, &h));
    ask(h, REQ(0x27, 0x01), 0);
    expect_nrc(0x27, 0x11);                          /* 0x11 comes before the session step */
    e32p_uds_deinit(h);
}

/* ---- 0x27 delay after a start -------------------------------------------------------- */

TEST_CASE("0x27 delay at start: without a load hook the delay runs, then a seed comes", TAG_UDS)
{
    g_cfg_sec_mode = SEC_HOOKS_NONE;
    e32p_uds_handle_t h;
    make_s3(&h, 60000u);
    TEST_ASSERT_EQUAL_INT(0, g_sec_loads);
    ask(h, REQ(0x10, 0x03), 100);                    /* the first time the server sees */
    ask(h, REQ(0x27, 0x01), 101);
    expect_nrc(0x27, 0x37);
    ask(h, REQ(0x27, 0x01), 100u + E32P_UDS_SECURITY_DELAY_MS - 1u);
    expect_nrc(0x27, 0x37);
    unlock(h, 100u + E32P_UDS_SECURITY_DELAY_MS);    /* the delay is over */
    e32p_uds_deinit(h);
}

TEST_CASE("0x27 delay at start: it runs from the first poll, not from time zero", TAG_UDS)
{
    g_cfg_sec_mode = SEC_HOOKS_NONE;
    e32p_uds_handle_t h;
    make_s3(&h, 60000u);
    TEST_ASSERT_EQUAL(ESP_OK, e32p_uds_poll(h, 5000));   /* the clock starts at boot, not at 0 */
    ask(h, REQ(0x10, 0x03), 5001);
    ask(h, REQ(0x27, 0x01), 5000u + E32P_UDS_SECURITY_DELAY_MS - 1u);
    expect_nrc(0x27, 0x37);
    unlock(h, 5000u + E32P_UDS_SECURITY_DELAY_MS);
    e32p_uds_deinit(h);
}

TEST_CASE("0x27 delay at start: a load hook that says 'not failed' gives a seed at once", TAG_UDS)
{
    g_cfg_sec_mode = SEC_HOOKS_NOT_FAILED;
    e32p_uds_handle_t h;
    make(&h);
    TEST_ASSERT_EQUAL_INT(1, g_sec_loads);
    ask(h, REQ(0x10, 0x03), 0);
    unlock(h, 1);
    e32p_uds_deinit(h);
}

TEST_CASE("0x27 delay at start: a load hook that says 'failed' runs the delay, then a seed", TAG_UDS)
{
    g_cfg_sec_mode = SEC_HOOKS_FAILED;
    e32p_uds_handle_t h;
    make_s3(&h, 60000u);
    TEST_ASSERT_EQUAL_INT(1, g_sec_loads);
    ask(h, REQ(0x10, 0x03), 0);
    ask(h, REQ(0x27, 0x01), 1);
    expect_nrc(0x27, 0x37);
    ask(h, REQ(0x10, 0x01), 2);                      /* a session change does not end it */
    ask(h, REQ(0x10, 0x03), 3);
    ask(h, REQ(0x27, 0x01), 4);
    expect_nrc(0x27, 0x37);
    unlock(h, E32P_UDS_SECURITY_DELAY_MS);           /* started at t = 0 */
    e32p_uds_deinit(h);
}

TEST_CASE("0x27 store hook: called only when the flag changes", TAG_UDS)
{
    g_cfg_sec_mode = SEC_HOOKS_NOT_FAILED;           /* stored value starts as 'not failed' */
    e32p_uds_handle_t h;
    make_s3(&h, 60000u);
    ask(h, REQ(0x10, 0x03), 0);
    unlock(h, 1);
    TEST_ASSERT_EQUAL_INT(0, g_sec_stores);          /* false -> false: nothing to save */
    uint8_t seed[E32P_UDS_SECURITY_LEN];
    ask(h, REQ(0x10, 0x03), 2);                      /* lock again */
    get_seed(h, 3, seed);
    send_key(h, seed, false, 4);
    expect_nrc(0x27, 0x35);
    TEST_ASSERT_EQUAL_INT(1, g_sec_stores);          /* first wrong key: true */
    TEST_ASSERT_TRUE(g_sec_last_stored);
    get_seed(h, 5, seed);
    send_key(h, seed, false, 6);
    expect_nrc(0x27, 0x35);
    TEST_ASSERT_EQUAL_INT(1, g_sec_stores);          /* true -> true: nothing to save */
    get_seed(h, 7, seed);
    send_key(h, seed, false, 8);
    expect_nrc(0x27, 0x36);                          /* third wrong key: delay starts */
    TEST_ASSERT_EQUAL_INT(1, g_sec_stores);
    unlock(h, 8u + E32P_UDS_SECURITY_DELAY_MS);
    TEST_ASSERT_EQUAL_INT(2, g_sec_stores);          /* right key: false */
    TEST_ASSERT_FALSE(g_sec_last_stored);
    e32p_uds_deinit(h);
}

TEST_CASE("0x27 store hook: a save that fails is tried again at the next outcome", TAG_UDS)
{
    g_cfg_sec_mode = SEC_HOOKS_NOT_FAILED;
    e32p_uds_handle_t h;
    make_s3(&h, 60000u);
    ask(h, REQ(0x10, 0x03), 0);
    uint8_t seed[E32P_UDS_SECURITY_LEN];
    g_sec_store_ok = false;                          /* the first save fails */
    get_seed(h, 1, seed);
    send_key(h, seed, false, 2);
    expect_nrc(0x27, 0x35);
    TEST_ASSERT_EQUAL_INT(1, g_sec_stores);          /* tried */
    g_sec_store_ok = true;
    get_seed(h, 3, seed);
    send_key(h, seed, false, 4);
    expect_nrc(0x27, 0x35);
    TEST_ASSERT_EQUAL_INT(2, g_sec_stores);          /* the saved value is unknown: again */
    TEST_ASSERT_TRUE(g_sec_last_stored);
    get_seed(h, 5, seed);
    send_key(h, seed, false, 6);
    expect_nrc(0x27, 0x36);
    TEST_ASSERT_EQUAL_INT(2, g_sec_stores);          /* saved now: true -> true needs nothing */
    e32p_uds_deinit(h);
}

TEST_CASE("0x27 store hook: a save that fails makes the saved value unknown, so the next outcome saves", TAG_UDS)
{
    g_cfg_sec_mode = SEC_HOOKS_NOT_FAILED;
    e32p_uds_handle_t h;
    make_s3(&h, 60000u);
    ask(h, REQ(0x10, 0x03), 0);
    uint8_t seed[E32P_UDS_SECURITY_LEN];
    get_seed(h, 1, seed);
    send_key(h, seed, false, 2);
    expect_nrc(0x27, 0x35);
    TEST_ASSERT_EQUAL_INT(1, g_sec_stores);          /* saved: true */
    g_sec_store_ok = false;                          /* the next save reports failure */
    get_seed(h, 3, seed);
    send_key(h, seed, true, 4);
    expect(REQ(0x67, 0x02));
    TEST_ASSERT_EQUAL_INT(2, g_sec_stores);          /* tried: false (it may or may not have landed) */
    TEST_ASSERT_FALSE(g_sec_last_stored);
    ask(h, REQ(0x10, 0x03), 4);                      /* lock again; the save still fails */
    get_seed(h, 4, seed);
    send_key(h, seed, true, 4);
    expect(REQ(0x67, 0x02));
    TEST_ASSERT_EQUAL_INT(3, g_sec_stores);          /* unknown: 'not failed' is saved again */
    g_sec_store_ok = true;
    ask(h, REQ(0x10, 0x03), 5);                      /* lock again */
    get_seed(h, 6, seed);
    send_key(h, seed, false, 7);
    expect_nrc(0x27, 0x35);
    TEST_ASSERT_EQUAL_INT(4, g_sec_stores);          /* the saved value is unknown: true is saved again */
    TEST_ASSERT_TRUE(g_sec_last_stored);
    e32p_uds_deinit(h);
}

TEST_CASE("0x27 store hook: a failed load and a failed save keep the next outcome saving", TAG_UDS)
{
    g_cfg_sec_mode = SEC_HOOKS_FAILED;
    e32p_uds_handle_t h;
    make_s3(&h, 60000u);
    ask(h, REQ(0x10, 0x03), 0);
    uint8_t seed[E32P_UDS_SECURITY_LEN];
    g_sec_store_ok = false;
    get_seed(h, E32P_UDS_SECURITY_DELAY_MS, seed);   /* the start delay is over */
    send_key(h, seed, false, E32P_UDS_SECURITY_DELAY_MS + 1u);
    expect_nrc(0x27, 0x35);
    TEST_ASSERT_EQUAL_INT(1, g_sec_stores);          /* tried, failed */
    get_seed(h, E32P_UDS_SECURITY_DELAY_MS + 2u, seed);
    send_key(h, seed, false, E32P_UDS_SECURITY_DELAY_MS + 3u);
    expect_nrc(0x27, 0x35);
    TEST_ASSERT_EQUAL_INT(2, g_sec_stores);          /* still unknown: again */
    e32p_uds_deinit(h);
}

TEST_CASE("0x27 store hook: after a 'failed' start the first right key clears the flag", TAG_UDS)
{
    g_cfg_sec_mode = SEC_HOOKS_FAILED;
    e32p_uds_handle_t h;
    make_s3(&h, 60000u);
    ask(h, REQ(0x10, 0x03), 0);
    unlock(h, E32P_UDS_SECURITY_DELAY_MS);
    TEST_ASSERT_EQUAL_INT(1, g_sec_stores);
    TEST_ASSERT_FALSE(g_sec_last_stored);
    e32p_uds_deinit(h);
}

TEST_CASE("0x27 store hook: after a failed load the first wrong key saves 'failed' again", TAG_UDS)
{
    g_cfg_sec_mode = SEC_HOOKS_FAILED;               /* load says 'failed' (or could not read) */
    e32p_uds_handle_t h;
    make_s3(&h, 60000u);
    ask(h, REQ(0x10, 0x03), 0);
    uint8_t seed[E32P_UDS_SECURITY_LEN];
    get_seed(h, E32P_UDS_SECURITY_DELAY_MS, seed);   /* the start delay is over */
    send_key(h, seed, false, E32P_UDS_SECURITY_DELAY_MS + 1u);
    expect_nrc(0x27, 0x35);
    TEST_ASSERT_EQUAL_INT(1, g_sec_stores);          /* the stored value is unknown: save it */
    TEST_ASSERT_TRUE(g_sec_last_stored);
    get_seed(h, E32P_UDS_SECURITY_DELAY_MS + 2u, seed);
    send_key(h, seed, false, E32P_UDS_SECURITY_DELAY_MS + 3u);
    expect_nrc(0x27, 0x35);
    TEST_ASSERT_EQUAL_INT(1, g_sec_stores);          /* known now: true -> true needs nothing */
    e32p_uds_deinit(h);
}

TEST_CASE("0x27 store hook: a key with no seed or the wrong length is not an outcome", TAG_UDS)
{
    g_cfg_sec_mode = SEC_HOOKS_NOT_FAILED;
    e32p_uds_handle_t h;
    make_s3(&h, 60000u);
    ask(h, REQ(0x10, 0x03), 0);
    uint8_t seed[E32P_UDS_SECURITY_LEN];
    const uint8_t early[E32P_UDS_SECURITY_LEN] = { 0 };
    send_key(h, early, false, 0);                    /* no seed, saved value known 'not failed' */
    expect_nrc(0x27, 0x24);
    TEST_ASSERT_EQUAL_INT(0, g_sec_stores);
    get_seed(h, 1, seed);
    ask(h, REQ(0x27, 0x02, 0x00), 1);                /* short key, same state */
    expect_nrc(0x27, 0x13);
    TEST_ASSERT_EQUAL_INT(0, g_sec_stores);
    send_key(h, seed, false, 2);                     /* one real outcome first: saved */
    expect_nrc(0x27, 0x35);
    TEST_ASSERT_EQUAL_INT(1, g_sec_stores);
    const uint8_t no_seed[E32P_UDS_SECURITY_LEN] = { 0 };
    send_key(h, no_seed, false, 3);                  /* the seed is spent: no seed to answer */
    expect_nrc(0x27, 0x24);
    TEST_ASSERT_EQUAL_INT(1, g_sec_stores);
    get_seed(h, 4, seed);
    ask(h, REQ(0x27, 0x02, 0x00), 5);                /* a key that is too short */
    expect_nrc(0x27, 0x13);
    TEST_ASSERT_EQUAL_INT(1, g_sec_stores);
    e32p_uds_deinit(h);
}

TEST_CASE("0x27 delay at start: a key during the start delay is 0x24 and saves nothing", TAG_UDS)
{
    g_cfg_sec_mode = SEC_HOOKS_FAILED;
    e32p_uds_handle_t h;
    make_s3(&h, 60000u);
    ask(h, REQ(0x10, 0x03), 0);
    const uint8_t no_seed[E32P_UDS_SECURITY_LEN] = { 0 };
    ask(h, REQ(0x27, 0x01), 1);                      /* the refused seed request, for real */
    expect_nrc(0x27, 0x37);
    send_key(h, no_seed, false, 1);                  /* the seed request was refused (0x37): no seed */
    expect_nrc(0x27, 0x24);
    TEST_ASSERT_EQUAL_INT(0, g_sec_stores);
    e32p_uds_deinit(h);
}

TEST_CASE("pending: a functional 10 04 (supported SID, unsupported sub-function) is 0x21", TAG_UDS)
{
    e32p_uds_handle_t h;
    make(&h);
    g_routine_pending = true;
    ask(h, REQ(0x31, 0x01, 0x02, 0x01), 0);
    ask_func(h, REQ(0x10, 0x04), 1);
    expect_nrc(0x10, 0x21);                          /* idle it would be silence: deviation (i) */
    e32p_uds_deinit(h);
}

/* ---- 0x19 / 0x14 trouble codes (slice 3.3) ---------------------------------- */

TEST_CASE("0x19 01 counts codes by status mask and states the DTC format", TAG_UDS)
{
    e32p_uds_handle_t h;
    make(&h);
    ask(h, REQ(0x19, 0x01, 0xFF), 0);
    expect(REQ(0x59, 0x01, 0x0D, 0x00, 0x00, 0x02));   /* U0073 has no status bit set */
    ask(h, REQ(0x19, 0x01, 0x08), 1);
    expect(REQ(0x59, 0x01, 0x0D, 0x00, 0x00, 0x01));   /* confirmed only */
    e32p_uds_deinit(h);
}

TEST_CASE("0x19 02 lists the codes that match the mask", TAG_UDS)
{
    e32p_uds_handle_t h;
    make(&h);
    ask(h, REQ(0x19, 0x02, 0x08), 0);
    expect(REQ(0x59, 0x02, 0x0D, 0x01, 0x33, 0x00, 0x09));
    ask(h, REQ(0x19, 0x02, 0x0C), 1);
    expect(REQ(0x59, 0x02, 0x0D, 0x01, 0x33, 0x00, 0x09, 0x04, 0x20, 0x00, 0x04));
    e32p_uds_deinit(h);
}

TEST_CASE("0x19 02 with nothing matching is still a positive answer", TAG_UDS)
{
    e32p_uds_handle_t h;
    make(&h);
    ask(h, REQ(0x19, 0x02, 0x00), 0);
    expect(REQ(0x59, 0x02, 0x0D));
    ask(h, REQ(0x19, 0x02, 0xF0), 1);                  /* bits the ECU does not report */
    expect(REQ(0x59, 0x02, 0x0D));
    e32p_uds_deinit(h);
}

TEST_CASE("0x19 ignores status bits outside the availability mask, both ways", TAG_UDS)
{
    e32p_uds_handle_t h;
    make(&h);
    g_dtc[0].status = 0x81u;                           /* bit 7 is not in the mask */
    ask(h, REQ(0x19, 0x02, 0x80), 0);
    expect(REQ(0x59, 0x02, 0x0D));                     /* cannot be matched on it  */
    ask(h, REQ(0x19, 0x02, 0x01), 1);
    expect(REQ(0x59, 0x02, 0x0D, 0x01, 0x33, 0x00, 0x01));   /* and is not reported */
    e32p_uds_deinit(h);
}

TEST_CASE("0x19 0A lists every code the ECU knows, whatever its status", TAG_UDS)
{
    e32p_uds_handle_t h;
    make(&h);
    ask(h, REQ(0x19, 0x0A), 0);
    expect(REQ(0x59, 0x0A, 0x0D, 0x01, 0x33, 0x00, 0x09, 0x04, 0x20, 0x00, 0x04,
               0xC0, 0x73, 0x00, 0x00));
    e32p_uds_deinit(h);
}

TEST_CASE("0x19: wrong lengths 0x13, an unknown report type 0x12", TAG_UDS)
{
    e32p_uds_handle_t h;
    make(&h);
    ask(h, REQ(0x19, 0x01), 0);
    expect_nrc(0x19, 0x13);
    ask(h, REQ(0x19, 0x02, 0x08, 0x00), 1);
    expect_nrc(0x19, 0x13);
    ask(h, REQ(0x19, 0x0A, 0x00), 2);
    expect_nrc(0x19, 0x13);
    ask(h, REQ(0x19, 0x05, 0xFF), 3);
    expect_nrc(0x19, 0x12);
    e32p_uds_deinit(h);
}

TEST_CASE("0x19: 63 codes fit in one answer, 64 answer 0x14", TAG_UDS)
{
    e32p_uds_handle_t h;
    make(&h);
    g_dtc_flood = 63u;                                 /* 3 + 63 x 4 = 255 bytes */
    ask(h, REQ(0x19, 0x02, 0xFF), 0);
    TEST_ASSERT_EQUAL_INT(1, g_msgs);
    TEST_ASSERT_EQUAL_UINT16(255u, g_msg_len[0]);
    TEST_ASSERT_EQUAL_HEX8(0x59, g_msg[0][0]);
    g_dtc_flood = 64u;                                 /* 259 > 256 */
    ask(h, REQ(0x19, 0x02, 0xFF), 1);
    expect_nrc(0x19, 0x14);
    e32p_uds_deinit(h);
}

TEST_CASE("0x14 FFFFFF clears through the provider, and the codes are gone", TAG_UDS)
{
    e32p_uds_handle_t h;
    make(&h);
    ask(h, REQ(0x14, 0xFF, 0xFF, 0xFF), 0);
    expect(REQ(0x54));
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFu, g_cleared_group);
    ask(h, REQ(0x19, 0x01, 0xFF), 1);
    expect(REQ(0x59, 0x01, 0x0D, 0x00, 0x00, 0x00));
    e32p_uds_deinit(h);
}

TEST_CASE("0x14: a group other than all is 0x31, a wrong length 0x13", TAG_UDS)
{
    e32p_uds_handle_t h;
    make(&h);
    ask(h, REQ(0x14, 0x01, 0x33, 0x00), 0);
    expect_nrc(0x14, 0x31);
    ask(h, REQ(0x14, 0xFF, 0xFF), 1);
    expect_nrc(0x14, 0x13);
    TEST_ASSERT_EQUAL_HEX32(0u, g_cleared_group);      /* the provider was never asked */
    e32p_uds_deinit(h);
}

TEST_CASE("0x14: a clear the application refuses is answered with its NRC", TAG_UDS)
{
    e32p_uds_handle_t h;
    make(&h);
    g_clear_nrc = 0x22u;
    ask(h, REQ(0x14, 0xFF, 0xFF, 0xFF), 0);
    expect_nrc(0x14, 0x22);
    ask(h, REQ(0x19, 0x01, 0xFF), 1);
    expect(REQ(0x59, 0x01, 0x0D, 0x00, 0x00, 0x02));   /* nothing was cleared */
    e32p_uds_deinit(h);
}

TEST_CASE("without a DTC provider 0x19 and 0x14 are not supported", TAG_UDS)
{
    g_msgs = 0;
    e32p_uds_config_t cfg = { .send = send };
    e32p_uds_handle_t h;
    TEST_ASSERT_EQUAL(ESP_OK, e32p_uds_init(&cfg, &h));
    ask(h, REQ(0x19, 0x01, 0xFF), 0);
    expect_nrc(0x19, 0x11);
    ask(h, REQ(0x14, 0xFF, 0xFF, 0xFF), 1);
    expect_nrc(0x14, 0x11);
    e32p_uds_deinit(h);
}

TEST_CASE("a provider without clear serves 0x19 but not 0x14", TAG_UDS)
{
    dtc_store_reset();
    g_msgs = 0;
    const e32p_uds_dtc_provider_t read_only = { DTC_AVAIL, E32P_UDS_DTC_FORMAT_ISO15031,
                                                dtc_count, dtc_at, NULL };
    e32p_uds_config_t cfg = { .send = send, .dtc = &read_only };
    e32p_uds_handle_t h;
    TEST_ASSERT_EQUAL(ESP_OK, e32p_uds_init(&cfg, &h));
    ask(h, REQ(0x19, 0x01, 0xFF), 0);
    expect(REQ(0x59, 0x01, 0x0D, 0x00, 0x00, 0x02));
    ask(h, REQ(0x14, 0xFF, 0xFF, 0xFF), 1);
    expect_nrc(0x14, 0x11);
    e32p_uds_deinit(h);
}

/* ---- 0x31 routine control (slice 3.3) --------------------------------------- */

TEST_CASE("0x31 01 starts a routine and passes its option record through", TAG_UDS)
{
    e32p_uds_handle_t h;
    make(&h);
    ask(h, REQ(0x31, 0x01, 0x02, 0x01, 0xAA, 0xBB), 0);
    expect(REQ(0x71, 0x01, 0x02, 0x01, 0x00));
    TEST_ASSERT_EQUAL_UINT16(2u, g_option_len);
    TEST_ASSERT_EQUAL_HEX8(0xAA, g_option[0]);
    TEST_ASSERT_EQUAL_HEX8(0xBB, g_option[1]);
    e32p_uds_deinit(h);
}

TEST_CASE("0x31 03 returns results, and 0x24 before the routine was started", TAG_UDS)
{
    e32p_uds_handle_t h;
    make(&h);
    ask(h, REQ(0x31, 0x03, 0x02, 0x01), 0);
    expect_nrc(0x31, 0x24);
    ask(h, REQ(0x31, 0x01, 0x02, 0x01), 1);
    expect(REQ(0x71, 0x01, 0x02, 0x01, 0x00));
    ask(h, REQ(0x31, 0x03, 0x02, 0x01), 2);
    expect(REQ(0x71, 0x03, 0x02, 0x01, 0x00, 0x42));
    e32p_uds_deinit(h);
}

TEST_CASE("0x31 03 takes no option record: extra bytes answer 0x13", TAG_UDS)
{
    e32p_uds_handle_t h;
    make(&h);
    ask(h, REQ(0x31, 0x01, 0x02, 0x01), 0);
    expect(REQ(0x71, 0x01, 0x02, 0x01, 0x00));
    ask(h, REQ(0x31, 0x03, 0x02, 0x01, 0xAA), 1);
    expect_nrc(0x31, 0x13);
    ask(h, REQ(0x31, 0x03, 0x02, 0x01), 2);
    expect(REQ(0x71, 0x03, 0x02, 0x01, 0x00, 0x42));
    e32p_uds_deinit(h);
}

TEST_CASE("0x31: unknown or out-of-session routine 0x31, short 0x13, stop 0x12", TAG_UDS)
{
    e32p_uds_handle_t h;
    make(&h);
    ask(h, REQ(0x31, 0x01, 0x12, 0x34), 0);
    expect_nrc(0x31, 0x31);
    ask(h, REQ(0x31, 0x01, 0x02, 0x02), 1);            /* extended only */
    expect_nrc(0x31, 0x31);
    ask(h, REQ(0x10, 0x03), 2);
    ask(h, REQ(0x31, 0x01, 0x02, 0x02), 3);
    expect(REQ(0x71, 0x01, 0x02, 0x02, 0x00));
    ask(h, REQ(0x31, 0x01, 0x02), 4);
    expect_nrc(0x31, 0x13);
    ask(h, REQ(0x31, 0x02, 0x02, 0x01), 5);            /* stop is not offered */
    expect_nrc(0x31, 0x12);
    e32p_uds_deinit(h);
}

TEST_CASE("0x31: a secured routine checks its session, then security, then runs", TAG_UDS)
{
    e32p_uds_handle_t h;
    make(&h);
    ask(h, REQ(0x31, 0x01, 0xFF, 0x00), 0);
    expect_nrc(0x31, 0x31);                            /* default: the session comes first */
    ask(h, REQ(0x10, 0x03), 1);
    ask(h, REQ(0x31, 0x01, 0xFF, 0x00), 2);
    expect_nrc(0x31, 0x33);
    unlock(h, 3);
    ask(h, REQ(0x31, 0x01, 0xFF, 0x00), 4);
    expect(REQ(0x71, 0x01, 0xFF, 0x00, 0x00));
    e32p_uds_deinit(h);
}

TEST_CASE("0x31 to an unknown routine, as a broadcast, stays silent", TAG_UDS)
{
    e32p_uds_handle_t h;
    make(&h);
    ask_func(h, REQ(0x31, 0x01, 0x12, 0x34), 0);
    expect_silence();
    e32p_uds_deinit(h);
}

TEST_CASE("0x31: a routine's own NRC is passed through", TAG_UDS)
{
    e32p_uds_handle_t h;
    make(&h);
    g_routine_nrc = 0x22u;
    ask(h, REQ(0x31, 0x01, 0x02, 0x01), 0);
    expect_nrc(0x31, 0x22);
    g_routine_nrc = 0u;
    e32p_uds_deinit(h);
}

TEST_CASE("without a routine table 0x31 is not supported", TAG_UDS)
{
    g_msgs = 0;
    e32p_uds_config_t cfg = { .send = send };
    e32p_uds_handle_t h;
    TEST_ASSERT_EQUAL(ESP_OK, e32p_uds_init(&cfg, &h));
    ask(h, REQ(0x31, 0x01, 0x02, 0x01), 0);
    expect_nrc(0x31, 0x11);
    e32p_uds_deinit(h);
}

/* ---- the 0x78 engine (slice 3.4) -------------------------------------------- */

static bool is_msg(int i, uint8_t a, uint8_t b, uint8_t c)
{
    return i < g_msgs && g_msg_len[i] == 3u && g_msg[i][0] == a && g_msg[i][1] == b && g_msg[i][2] == c;
}

static void poll_at(e32p_uds_handle_t h, uint32_t now_ms)
{
    g_msgs = 0;
    TEST_ASSERT_EQUAL(ESP_OK, e32p_uds_poll(h, now_ms));
}

TEST_CASE("pending: a callback still working answers 0x78 at once", TAG_UDS)
{
    e32p_uds_handle_t h;
    make(&h);
    g_routine_pending = true;
    ask(h, REQ(0x31, 0x01, 0x02, 0x01), 100);
    expect_nrc(0x31, 0x78);
    e32p_uds_deinit(h);
}

TEST_CASE("pending: 0x78 repeats at 80 % of P2*, so never later than P2*", TAG_UDS)
{
    e32p_uds_handle_t h;
    make(&h);
    g_routine_pending = true;
    ask(h, REQ(0x31, 0x01, 0x02, 0x01), 100);
    poll_at(h, 4099);
    expect_silence();
    poll_at(h, 4100);                                   /* 4000 ms after the first */
    expect_nrc(0x31, 0x78);
    poll_at(h, 8099);
    expect_silence();
    poll_at(h, 8100);
    expect_nrc(0x31, 0x78);
    e32p_uds_deinit(h);
}

TEST_CASE("pending: complete() sends the final answer on top of the echo, once", TAG_UDS)
{
    e32p_uds_handle_t h;
    make(&h);
    g_routine_pending = true;
    ask(h, REQ(0x31, 0x01, 0x02, 0x01), 0);
    g_msgs = 0;
    const uint8_t result[] = { 0x00, 0x42 };
    TEST_ASSERT_EQUAL(ESP_OK, e32p_uds_complete(h, E32P_UDS_NRC_OK, result, sizeof(result), 2000));
    expect(REQ(0x71, 0x01, 0x02, 0x01, 0x00, 0x42));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, e32p_uds_complete(h, E32P_UDS_NRC_OK, NULL, 0, 2001));
    poll_at(h, 9000);
    expect_silence();                                   /* no more 0x78 */
    e32p_uds_deinit(h);
}

TEST_CASE("pending: the final answer goes out even when none was asked for", TAG_UDS)
{
    e32p_uds_handle_t h;
    make(&h);
    ask(h, REQ(0x31, 0x81, 0x02, 0x01), 0);             /* not pending: suppressed */
    expect_silence();
    g_routine_pending = true;
    ask(h, REQ(0x31, 0x81, 0x02, 0x01), 1);
    expect_nrc(0x31, 0x78);
    g_msgs = 0;
    TEST_ASSERT_EQUAL(ESP_OK, e32p_uds_complete(h, E32P_UDS_NRC_OK, NULL, 0, 2));
    expect(REQ(0x71, 0x01, 0x02, 0x01));                /* the tester was told to wait */
    e32p_uds_deinit(h);
}

TEST_CASE("pending: complete() with an NRC sends that NRC", TAG_UDS)
{
    e32p_uds_handle_t h;
    make(&h);
    g_routine_pending = true;
    ask(h, REQ(0x31, 0x01, 0x02, 0x01), 0);
    g_msgs = 0;
    TEST_ASSERT_EQUAL(ESP_OK, e32p_uds_complete(h, E32P_UDS_NRC_PROGRAMMING_FAILURE, NULL, 0, 1));
    expect_nrc(0x31, 0x72);
    e32p_uds_deinit(h);
}

TEST_CASE("pending: other requests are 0x21 meanwhile, a silent 3E 80 is not", TAG_UDS)
{
    e32p_uds_handle_t h;
    make(&h);
    g_routine_pending = true;
    ask(h, REQ(0x31, 0x01, 0x02, 0x01), 0);
    ask(h, REQ(0x22, 0xF1, 0x90), 1);
    expect_nrc(0x22, 0x21);
    ask(h, REQ(0x3E, 0x80), 2);
    expect_silence();
    ask(h, REQ(0x3E, 0x00), 3);
    expect_nrc(0x3E, 0x21);
    g_msgs = 0;
    TEST_ASSERT_EQUAL(ESP_OK, e32p_uds_complete(h, E32P_UDS_NRC_OK, NULL, 0, 4));
    expect(REQ(0x71, 0x01, 0x02, 0x01));
    ask(h, REQ(0x3E, 0x00), 5);                         /* free again */
    expect(REQ(0x7E, 0x00));
    e32p_uds_deinit(h);
}

TEST_CASE("pending: an unsupported service is 0x11 physically and silent on broadcast", TAG_UDS)
{
    e32p_uds_handle_t h;
    make(&h);
    g_routine_pending = true;
    ask(h, REQ(0x31, 0x01, 0x02, 0x01), 0);
    ask(h, REQ(0x99), 1);
    expect_nrc(0x99, 0x11);                          /* not 0x21: the service does not exist */
    ask_func(h, REQ(0x99), 2);
    expect_silence();                                /* a broadcast gets no 0x11 */
    ask(h, REQ(0x85, 0x01), 3);                      /* a real SID this server does not offer */
    expect_nrc(0x85, 0x11);
    ask_func(h, REQ(0x85, 0x01), 4);
    expect_silence();
    e32p_uds_deinit(h);
}

TEST_CASE("pending: a supported service is 0x21, and so is its broadcast", TAG_UDS)
{
    e32p_uds_handle_t h;
    make(&h);
    g_routine_pending = true;
    ask(h, REQ(0x31, 0x01, 0x02, 0x01), 0);
    ask(h, REQ(0x22, 0xF1, 0x90), 1);
    expect_nrc(0x22, 0x21);
    ask_func(h, REQ(0x22, 0xF1, 0x90), 2);
    expect_nrc(0x22, 0x21);
    e32p_uds_deinit(h);
}

TEST_CASE("pending: S3 is held while waiting and restarts with the answer", TAG_UDS)
{
    e32p_uds_handle_t h;
    make(&h);
    ask(h, REQ(0x10, 0x03), 0);
    g_routine_pending = true;
    ask(h, REQ(0x31, 0x01, 0x02, 0x02), 1000);
    poll_at(h, 7000);                                   /* S3 would have run out at 6000 */
    TEST_ASSERT_EQUAL(E32P_UDS_SESSION_EXTENDED, e32p_uds_session(h));
    TEST_ASSERT_EQUAL(ESP_OK, e32p_uds_complete(h, E32P_UDS_NRC_OK, NULL, 0, 8000));
    poll_at(h, 12999);
    TEST_ASSERT_EQUAL(E32P_UDS_SESSION_EXTENDED, e32p_uds_session(h));
    poll_at(h, 13000);                                  /* 5 s after the answer */
    TEST_ASSERT_EQUAL(E32P_UDS_SESSION_DEFAULT, e32p_uds_session(h));
    e32p_uds_deinit(h);
}

TEST_CASE("pending: past the cap the request is answered 0x10 and released", TAG_UDS)
{
    e32p_uds_handle_t h;
    g_cfg_pending_max_ms = 9000u;
    make(&h);
    g_routine_pending = true;
    ask(h, REQ(0x31, 0x01, 0x02, 0x01), 0);
    poll_at(h, 8999);
    TEST_ASSERT_FALSE(is_msg(0, 0x7F, 0x31, 0x10));
    poll_at(h, 9000);
    expect_nrc(0x31, 0x10);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, e32p_uds_complete(h, E32P_UDS_NRC_OK, NULL, 0, 9001));
    ask(h, REQ(0x3E, 0x00), 9002);                      /* not busy any more */
    expect(REQ(0x7E, 0x00));
    e32p_uds_deinit(h);
}

TEST_CASE("pending: the cap defaults to 60 s", TAG_UDS)
{
    e32p_uds_handle_t h;
    make(&h);
    g_routine_pending = true;
    ask(h, REQ(0x31, 0x01, 0x02, 0x01), 0);
    poll_at(h, 59999);
    TEST_ASSERT_FALSE(is_msg(0, 0x7F, 0x31, 0x10));
    poll_at(h, 60000);
    expect_nrc(0x31, 0x10);
    e32p_uds_deinit(h);
}

TEST_CASE("pending: a DID write and a clear can finish later too", TAG_UDS)
{
    e32p_uds_handle_t h;
    make(&h);
    ask(h, REQ(0x10, 0x03), 0);
    g_write_pending = true;
    ask(h, REQ(0x2E, 0x01, 0x00, 0xAA, 0xBB, 0xCC, 0xDD), 1);
    expect_nrc(0x2E, 0x78);
    g_msgs = 0;
    TEST_ASSERT_EQUAL(ESP_OK, e32p_uds_complete(h, E32P_UDS_NRC_OK, NULL, 0, 2));
    expect(REQ(0x6E, 0x01, 0x00));
    g_clear_pending = true;
    ask(h, REQ(0x14, 0xFF, 0xFF, 0xFF), 3);
    expect_nrc(0x14, 0x78);
    g_msgs = 0;
    TEST_ASSERT_EQUAL(ESP_OK, e32p_uds_complete(h, E32P_UDS_NRC_OK, NULL, 0, 4));
    expect(REQ(0x54));
    e32p_uds_deinit(h);
}

/* ---- 0x34 / 0x36 / 0x37 download (slice 3.4) -------------------------------- */

static void prog_unlocked(e32p_uds_handle_t h, uint32_t now_ms)
{
    ask(h, REQ(0x10, 0x02), now_ms);
    unlock(h, now_ms);
}

static void request_download(e32p_uds_handle_t h, uint32_t size, uint32_t now_ms)
{
    const uint8_t req[11] = { 0x34, 0x00, 0x44, 0x00, 0x00, 0x00, 0x00,
                              (uint8_t)(size >> 24), (uint8_t)(size >> 16),
                              (uint8_t)(size >> 8), (uint8_t)size };
    ask(h, req, sizeof(req), now_ms);
}

static void send_block(e32p_uds_handle_t h, uint8_t counter, const uint8_t *data, uint16_t n,
                       uint32_t now_ms)
{
    static uint8_t req[2 + 1100];
    req[0] = 0x36;
    req[1] = counter;
    memcpy(&req[2], data, n);
    ask(h, req, (uint16_t)(2u + n), now_ms);
}

static void expect_block_ack(uint8_t counter)
{
    const uint8_t want[2] = { 0x76, counter };
    expect(want, sizeof(want));
}

static uint8_t g_src[SINK_CAP];

static void image_fill(uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        g_src[i] = (uint8_t)(i * 7u + 3u);
    }
    g_sink_good_crc = crc32_of(g_src, n);
}

TEST_CASE("0x34: programming session only (0x7F), and security first (0x33)", TAG_UDS)
{
    e32p_uds_handle_t h;
    make(&h);
    request_download(h, 16u, 0);
    expect_nrc(0x34, 0x7F);
    ask(h, REQ(0x10, 0x02), 1);
    request_download(h, 16u, 2);
    expect_nrc(0x34, 0x33);
    TEST_ASSERT_EQUAL_INT(0, g_sink_begins);
    e32p_uds_deinit(h);
}

TEST_CASE("0x34 accepts a download and advertises 1026-byte blocks", TAG_UDS)
{
    e32p_uds_handle_t h;
    make(&h);
    prog_unlocked(h, 0);
    request_download(h, SINK_CAP, 1);
    expect(REQ(0x74, 0x20, 0x04, 0x02));
    TEST_ASSERT_EQUAL_INT(1, g_sink_begins);
    e32p_uds_deinit(h);
}

TEST_CASE("0x34 refuses what it cannot take, each with its own NRC", TAG_UDS)
{
    e32p_uds_handle_t h;
    make(&h);
    prog_unlocked(h, 0);
    ask(h, REQ(0x34, 0x10, 0x44, 0, 0, 0, 0, 0, 0, 0, 16), 1);    /* compressed */
    expect_nrc(0x34, 0x31);
    ask(h, REQ(0x34, 0x00, 0x55, 0, 0, 0, 0, 0, 0, 0, 0, 0, 16), 2);   /* 5-byte fields */
    expect_nrc(0x34, 0x31);
    ask(h, REQ(0x34, 0x00, 0x44, 0, 0, 0, 0, 0, 16), 3);          /* too short for 0x44 */
    expect_nrc(0x34, 0x13);
    ask(h, REQ(0x34, 0x00, 0x44, 0, 0, 0x10, 0, 0, 0, 0, 16), 4); /* address not 0 */
    expect_nrc(0x34, 0x31);
    request_download(h, 0u, 5);                                    /* nothing to send */
    expect_nrc(0x34, 0x31);
    request_download(h, SINK_CAP + 1u, 6);                         /* bigger than the sink */
    expect_nrc(0x34, 0x70);
    TEST_ASSERT_EQUAL_INT(0, g_sink_begins);
    request_download(h, 16u, 7);
    expect(REQ(0x74, 0x20, 0x04, 0x02));
    request_download(h, 16u, 8);                                   /* one at a time */
    expect_nrc(0x34, 0x22);
    e32p_uds_deinit(h);
}

TEST_CASE("download: a 4 KB image arrives in 1024-byte blocks and validates", TAG_UDS)
{
    e32p_uds_handle_t h;
    make(&h);
    image_fill(SINK_CAP);
    prog_unlocked(h, 0);
    request_download(h, SINK_CAP, 1);
    for (uint8_t b = 0; b < 4u; b++) {
        send_block(h, (uint8_t)(b + 1u), &g_src[b * 1024u], 1024u, 2u + b);
        expect_block_ack((uint8_t)(b + 1u));
    }
    ask(h, REQ(0x37), 10);
    expect(REQ(0x77));
    TEST_ASSERT_EQUAL_UINT32(SINK_CAP, g_image_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(g_src, g_image, SINK_CAP);
    TEST_ASSERT_EQUAL_INT(1, g_sink_ends);
    TEST_ASSERT_EQUAL_INT(0, g_sink_aborts);
    e32p_uds_deinit(h);
}

TEST_CASE("0x36: a repeated block is acknowledged and not written twice", TAG_UDS)
{
    e32p_uds_handle_t h;
    make(&h);
    image_fill(32u);
    prog_unlocked(h, 0);
    request_download(h, 32u, 1);
    send_block(h, 0x01, &g_src[0], 16u, 2);
    expect_block_ack(0x01);
    send_block(h, 0x01, &g_src[0], 16u, 3);             /* our answer was lost, say */
    expect_block_ack(0x01);
    TEST_ASSERT_EQUAL_INT(1, g_sink_writes);
    send_block(h, 0x02, &g_src[16], 16u, 4);
    expect_block_ack(0x02);
    ask(h, REQ(0x37), 5);
    expect(REQ(0x77));
    e32p_uds_deinit(h);
}

TEST_CASE("0x36: no download 0x24, skipped counter 0x73, too long 0x13, too many 0x71", TAG_UDS)
{
    e32p_uds_handle_t h;
    make(&h);
    image_fill(SINK_CAP);
    prog_unlocked(h, 0);
    send_block(h, 0x01, g_src, 4u, 1);
    expect_nrc(0x36, 0x24);
    request_download(h, 10u, 2);
    send_block(h, 0x02, g_src, 4u, 3);
    expect_nrc(0x36, 0x73);
    send_block(h, 0x01, g_src, 1025u, 4);               /* 1027 bytes with SID + counter */
    expect_nrc(0x36, 0x13);
    send_block(h, 0x01, g_src, 11u, 5);                 /* announced 10 */
    expect_nrc(0x36, 0x71);
    TEST_ASSERT_EQUAL_INT(0, g_sink_writes);
    e32p_uds_deinit(h);
}

TEST_CASE("0x36: the block counter wraps from FF to 00", TAG_UDS)
{
    e32p_uds_handle_t h;
    make(&h);
    image_fill(300u);
    prog_unlocked(h, 0);
    request_download(h, 300u, 1);
    for (uint32_t i = 0; i < 300u; i++) {
        const uint8_t counter = (uint8_t)(i + 1u);      /* 01 .. FF, 00, 01 .. */
        send_block(h, counter, &g_src[i], 1u, 2u + i);
        expect_block_ack(counter);
    }
    ask(h, REQ(0x37), 400);
    expect(REQ(0x77));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(g_src, g_image, 300u);
    e32p_uds_deinit(h);
}

TEST_CASE("0x36: a write that fails, now or later, does not advance the counter", TAG_UDS)
{
    e32p_uds_handle_t h;
    make(&h);
    image_fill(64u);
    prog_unlocked(h, 0);
    request_download(h, 64u, 1);
    g_sink_write_nrc = 0x72u;
    send_block(h, 0x01, &g_src[0], 16u, 2);
    expect_nrc(0x36, 0x72);
    g_sink_write_nrc = 0u;
    send_block(h, 0x01, &g_src[0], 16u, 3);             /* still block 1 */
    expect_block_ack(0x01);
    g_sink_write_pending = true;
    send_block(h, 0x02, &g_src[16], 16u, 4);
    expect_nrc(0x36, 0x78);
    g_msgs = 0;
    TEST_ASSERT_EQUAL(ESP_OK, e32p_uds_complete(h, E32P_UDS_NRC_PROGRAMMING_FAILURE, NULL, 0, 5));
    expect_nrc(0x36, 0x72);
    g_sink_write_pending = false;
    send_block(h, 0x03, &g_src[16], 16u, 6);            /* block 2 never counted */
    expect_nrc(0x36, 0x73);
    g_sink_write_pending = true;
    send_block(h, 0x02, &g_src[16], 16u, 7);
    expect_nrc(0x36, 0x78);
    g_msgs = 0;
    TEST_ASSERT_EQUAL(ESP_OK, e32p_uds_complete(h, E32P_UDS_NRC_OK, NULL, 0, 8));
    expect_block_ack(0x02);
    g_sink_write_pending = false;
    send_block(h, 0x03, &g_src[32], 16u, 9);            /* counted at completion */
    expect_block_ack(0x03);
    e32p_uds_deinit(h);
}

TEST_CASE("0x37 before every byte arrived is 0x24, and the download carries on", TAG_UDS)
{
    e32p_uds_handle_t h;
    make(&h);
    image_fill(32u);
    prog_unlocked(h, 0);
    request_download(h, 32u, 1);
    send_block(h, 0x01, &g_src[0], 16u, 2);
    ask(h, REQ(0x37), 3);
    expect_nrc(0x37, 0x24);
    send_block(h, 0x02, &g_src[16], 16u, 4);
    expect_block_ack(0x02);
    ask(h, REQ(0x37), 5);
    expect(REQ(0x77));
    e32p_uds_deinit(h);
}

TEST_CASE("0x37: a corrupted image is 0x72, and the download ends with an abort", TAG_UDS)
{
    e32p_uds_handle_t h;
    make(&h);
    image_fill(32u);
    g_src[5] ^= 0xFFu;                                  /* corrupt it in flight */
    prog_unlocked(h, 0);
    request_download(h, 32u, 1);
    send_block(h, 0x01, &g_src[0], 32u, 2);
    ask(h, REQ(0x37), 3);
    expect_nrc(0x37, 0x72);
    TEST_ASSERT_EQUAL_INT(1, g_sink_aborts);
    send_block(h, 0x02, &g_src[0], 4u, 4);
    expect_nrc(0x36, 0x24);                             /* no download any more */
    e32p_uds_deinit(h);
}

TEST_CASE("download: begin and validation can finish later", TAG_UDS)
{
    e32p_uds_handle_t h;
    make(&h);
    image_fill(16u);
    prog_unlocked(h, 0);
    g_sink_begin_pending = true;                        /* an erase, in real life */
    request_download(h, 16u, 1);
    expect_nrc(0x34, 0x78);
    send_block(h, 0x01, g_src, 16u, 2);
    expect_nrc(0x36, 0x21);                             /* busy */
    g_msgs = 0;
    TEST_ASSERT_EQUAL(ESP_OK, e32p_uds_complete(h, E32P_UDS_NRC_OK, NULL, 0, 3000));
    expect(REQ(0x74, 0x20, 0x04, 0x02));
    send_block(h, 0x01, g_src, 16u, 3001);
    expect_block_ack(0x01);
    g_sink_end_pending = true;
    ask(h, REQ(0x37), 3002);
    expect_nrc(0x37, 0x78);
    g_msgs = 0;
    TEST_ASSERT_EQUAL(ESP_OK, e32p_uds_complete(h, E32P_UDS_NRC_OK, NULL, 0, 4000));
    expect(REQ(0x77));
    TEST_ASSERT_EQUAL_INT(0, g_sink_aborts);
    e32p_uds_deinit(h);
}

TEST_CASE("download: leaving the session abandons it and aborts the sink", TAG_UDS)
{
    e32p_uds_handle_t h;
    make(&h);
    image_fill(32u);
    prog_unlocked(h, 0);
    request_download(h, 32u, 1);
    send_block(h, 0x01, g_src, 16u, 2);
    ask(h, REQ(0x10, 0x03), 3);
    TEST_ASSERT_EQUAL_INT(1, g_sink_aborts);
    prog_unlocked(h, 4);
    send_block(h, 0x02, &g_src[16], 16u, 5);
    expect_nrc(0x36, 0x24);
    e32p_uds_deinit(h);
}

TEST_CASE("download: an erase given up at the cap aborts the sink", TAG_UDS)
{
    e32p_uds_handle_t h;
    g_cfg_pending_max_ms = 9000u;
    make(&h);
    prog_unlocked(h, 0);
    g_sink_begin_pending = true;
    request_download(h, 16u, 1);
    poll_at(h, 9001);
    expect_nrc(0x34, 0x10);
    TEST_ASSERT_EQUAL_INT(1, g_sink_aborts);
    e32p_uds_deinit(h);
}

TEST_CASE("without a sink the download services are not supported", TAG_UDS)
{
    g_msgs = 0;
    e32p_uds_config_t cfg = { .send = send };
    e32p_uds_handle_t h;
    TEST_ASSERT_EQUAL(ESP_OK, e32p_uds_init(&cfg, &h));
    request_download(h, 16u, 0);
    expect_nrc(0x34, 0x11);
    ask(h, REQ(0x36, 0x01, 0xAA), 1);
    expect_nrc(0x36, 0x11);
    ask(h, REQ(0x37), 2);
    expect_nrc(0x37, 0x11);
    e32p_uds_deinit(h);
}
