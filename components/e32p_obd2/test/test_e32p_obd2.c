/**
 * @file test_e32p_obd2.c
 * @brief Unity tests for e32p_obd2 — fully headless. No CAN, no clock, no task:
 *        a request goes in, a response comes out, and the case checks the bytes
 *        against SAE J1979.
 *
 * Slice 2.1 covers modes 0x01 and 0x09; trouble codes arrive with slice 2.2.
 */
#include "unity.h"
#include <string.h>
#include "e32p_nvs.h"
#include "e32p_obd2.h"

#define VIN_DEMO "E32PDEMO000000001"
#define DTC_NS   "obd2test"

/* The value provider. g_refuse makes it decline, which is how an application
   says "I cannot produce this right now". */
static bool    g_refuse;
static int     g_reads;
static uint8_t g_rpm_hi, g_rpm_lo;   /* moved by the freeze-frame cases */

static esp_err_t read_pid(uint8_t pid, uint8_t *out, uint8_t len, void *user)
{
    (void)user;
    g_reads++;
    if (g_refuse) {
        return ESP_FAIL;
    }
    switch (pid) {
    case 0x05: out[0] = 130u;                          return ESP_OK;  /* 90 degC  */
    case 0x0C: out[0] = g_rpm_hi; out[1] = g_rpm_lo;   return ESP_OK;  /* rpm * 4  */
    case 0x0D: out[0] = 88u;                           return ESP_OK;  /* 88 km/h  */
    default:   (void)len;                              return ESP_FAIL;
    }
}

static const e32p_obd2_pid_t k_pids[] = {
    { 0x05, 1 },   /* coolant temperature */
    { 0x0C, 2 },   /* engine RPM          */
    { 0x0D, 1 },   /* vehicle speed       */
    { 0x1C, 1 },   /* OBD standard        */
};

/* Helpers take an out-parameter instead of returning a value: an assert inside a
   value-returning helper is not portable across Unity runners. */
static void make_server(const e32p_obd2_pid_t *pids, uint8_t count, const char *vin,
                        e32p_obd2_handle_t *out)
{
    g_refuse = false;
    g_reads = 0;
    g_rpm_hi = 0x1Au;
    g_rpm_lo = 0xF8u;
    e32p_obd2_config_t cfg = {
        .pids = pids, .pid_count = count, .on_read = read_pid, .user = NULL,
        .vin = vin, .obd_standard = 0x06u, .nvs_namespace = NULL,
    };
    *out = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, e32p_obd2_init(&cfg, out));
    TEST_ASSERT_NOT_NULL(*out);
}

/** @brief Run one request; @p n gets the response length (0 = stay silent). */
static void ask(e32p_obd2_handle_t h, e32p_obd2_addressing_t addressing,
                const uint8_t *req, uint16_t req_len, uint8_t *resp, uint16_t *n)
{
    *n = 0xFFFFu;
    TEST_ASSERT_EQUAL(ESP_OK, e32p_obd2_request(h, addressing, req, req_len,
                                                resp, E32P_OBD2_RESP_MAX, n));
}

/* ========================= 1-3: lifecycle and args ======================== */

TEST_CASE("obd2: init rejects bad configuration", "[e32p_obd2]")
{
    e32p_obd2_handle_t h = NULL;
    e32p_obd2_config_t cfg = { .pids = k_pids, .pid_count = 4, .on_read = read_pid,
                               .vin = VIN_DEMO, .obd_standard = 0x06u };
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_obd2_init(NULL, &h));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_obd2_init(&cfg, NULL));

    e32p_obd2_config_t no_reader = cfg;
    no_reader.on_read = NULL;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_obd2_init(&no_reader, &h));

    e32p_obd2_config_t short_vin = cfg;
    short_vin.vin = "TOOSHORT";
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_obd2_init(&short_vin, &h));

    /* A bitmask PID in the table would let the table and the mask disagree. */
    static const e32p_obd2_pid_t reserved[] = { { 0x20, 4 } };
    e32p_obd2_config_t bad_pid = cfg;
    bad_pid.pids = reserved;
    bad_pid.pid_count = 1;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_obd2_init(&bad_pid, &h));

    static const e32p_obd2_pid_t dup[] = { { 0x0C, 2 }, { 0x0C, 2 } };
    e32p_obd2_config_t bad_dup = cfg;
    bad_dup.pids = dup;
    bad_dup.pid_count = 2;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_obd2_init(&bad_dup, &h));
}

TEST_CASE("obd2: a response buffer below the maximum is rejected", "[e32p_obd2]")
{
    e32p_obd2_handle_t h;
    make_server(k_pids, 4, VIN_DEMO, &h);
    const uint8_t req[] = { 0x01, 0x0C };
    uint8_t small[8];
    uint16_t n = 0;
    /* The VIN response is 20 bytes; a caller with a smaller buffer is a bug we
       refuse at the door rather than truncating a response onto the bus. */
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      e32p_obd2_request(h, E32P_OBD2_ADDR_PHYSICAL, req, 2, small, sizeof(small), &n));
    e32p_obd2_deinit(h);
}

TEST_CASE("obd2: an empty request answers nothing", "[e32p_obd2]")
{
    e32p_obd2_handle_t h;
    make_server(k_pids, 4, VIN_DEMO, &h);
    uint8_t resp[E32P_OBD2_RESP_MAX];
    uint16_t n = 0;
    const uint8_t req[] = { 0x00 };
    ask(h, E32P_OBD2_ADDR_PHYSICAL, req, 0, resp, &n);
    TEST_ASSERT_EQUAL(0, n);
    e32p_obd2_deinit(h);
}

/* ===================== 4-7: mode 0x01 supported PIDs ===================== */

TEST_CASE("obd2: PID 0x00 bitmask is derived from the table", "[e32p_obd2]")
{
    e32p_obd2_handle_t h;
    make_server(k_pids, 4, VIN_DEMO, &h);
    uint8_t resp[E32P_OBD2_RESP_MAX];
    uint16_t n = 0;
    const uint8_t req[] = { 0x01, 0x00 };
    ask(h, E32P_OBD2_ADDR_PHYSICAL, req, 2, resp, &n);
    TEST_ASSERT_EQUAL(6, n);
    TEST_ASSERT_EQUAL_HEX8(0x41, resp[0]);
    TEST_ASSERT_EQUAL_HEX8(0x00, resp[1]);
    /* Byte A covers PIDs 0x01-0x08, B 0x09-0x10, C 0x11-0x18, D 0x19-0x20, each
       MSB first: 0x05 -> A bit 0x08; 0x0C and 0x0D -> B 0x10 and 0x08;
       0x1C -> D 0x10. The bottom bit of D is "PID 0x20 supported", and nothing
       in this table lives above 0x20, so it stays clear. */
    TEST_ASSERT_EQUAL_HEX8(0x08, resp[2]);
    TEST_ASSERT_EQUAL_HEX8(0x18, resp[3]);
    TEST_ASSERT_EQUAL_HEX8(0x00, resp[4]);
    TEST_ASSERT_EQUAL_HEX8(0x10, resp[5]);
    e32p_obd2_deinit(h);
}

TEST_CASE("obd2: PID 0x20 is advertised only when a PID lives above it", "[e32p_obd2]")
{
    static const e32p_obd2_pid_t with_high[] = { { 0x0C, 2 }, { 0x31, 2 } };
    e32p_obd2_handle_t h;
    make_server(with_high, 2, NULL, &h);
    uint8_t resp[E32P_OBD2_RESP_MAX];
    uint16_t n = 0;

    const uint8_t req00[] = { 0x01, 0x00 };
    ask(h, E32P_OBD2_ADDR_PHYSICAL, req00, 2, resp, &n);
    TEST_ASSERT_EQUAL(6, n);
    TEST_ASSERT_EQUAL_HEX8(0x01, resp[5] & 0x01u);   /* "the next block exists" */

    const uint8_t req20[] = { 0x01, 0x20 };
    ask(h, E32P_OBD2_ADDR_PHYSICAL, req20, 2, resp, &n);
    TEST_ASSERT_EQUAL(6, n);
    TEST_ASSERT_EQUAL_HEX8(0x20, resp[1]);
    TEST_ASSERT_EQUAL_HEX8(0x00, resp[2]);           /* this block starts at 0x21 */
    TEST_ASSERT_EQUAL_HEX8(0x80, resp[4]);           /* 0x31 -> third byte, top bit */
    TEST_ASSERT_EQUAL_HEX8(0x00, resp[5]);           /* nothing above 0x40 */
    e32p_obd2_deinit(h);
}

TEST_CASE("obd2: PID 0x20 is unsupported when the table stops below it", "[e32p_obd2]")
{
    e32p_obd2_handle_t h;
    make_server(k_pids, 4, VIN_DEMO, &h);
    uint8_t resp[E32P_OBD2_RESP_MAX];
    uint16_t n = 0;
    const uint8_t req[] = { 0x01, 0x20 };
    ask(h, E32P_OBD2_ADDR_PHYSICAL, req, 2, resp, &n);
    TEST_ASSERT_EQUAL(3, n);
    TEST_ASSERT_EQUAL_HEX8(0x7F, resp[0]);
    TEST_ASSERT_EQUAL_HEX8(0x01, resp[1]);
    TEST_ASSERT_EQUAL_HEX8(0x12, resp[2]);
    e32p_obd2_deinit(h);
}

/* ======================= 8-11: mode 0x01 live data ======================= */

TEST_CASE("obd2: a supported PID answers with the provider's bytes", "[e32p_obd2]")
{
    e32p_obd2_handle_t h;
    make_server(k_pids, 4, VIN_DEMO, &h);
    uint8_t resp[E32P_OBD2_RESP_MAX];
    uint16_t n = 0;

    const uint8_t rpm[] = { 0x01, 0x0C };
    ask(h, E32P_OBD2_ADDR_PHYSICAL, rpm, 2, resp, &n);
    TEST_ASSERT_EQUAL(4, n);
    TEST_ASSERT_EQUAL_HEX8(0x41, resp[0]);
    TEST_ASSERT_EQUAL_HEX8(0x0C, resp[1]);
    TEST_ASSERT_EQUAL_HEX8(0x1A, resp[2]);
    TEST_ASSERT_EQUAL_HEX8(0xF8, resp[3]);   /* (0x1AF8)/4 = 1726 rpm */

    const uint8_t speed[] = { 0x01, 0x0D };
    ask(h, E32P_OBD2_ADDR_PHYSICAL, speed, 2, resp, &n);
    TEST_ASSERT_EQUAL(3, n);
    TEST_ASSERT_EQUAL_HEX8(88, resp[2]);
    e32p_obd2_deinit(h);
}

TEST_CASE("obd2: PID 0x1C comes from the configuration, not the provider", "[e32p_obd2]")
{
    e32p_obd2_handle_t h;
    make_server(k_pids, 4, VIN_DEMO, &h);
    uint8_t resp[E32P_OBD2_RESP_MAX];
    uint16_t n = 0;
    const int before = g_reads;
    const uint8_t req[] = { 0x01, 0x1C };
    ask(h, E32P_OBD2_ADDR_PHYSICAL, req, 2, resp, &n);
    TEST_ASSERT_EQUAL(3, n);
    TEST_ASSERT_EQUAL_HEX8(0x06, resp[2]);   /* EOBD */
    TEST_ASSERT_EQUAL(before, g_reads);      /* the application was not asked */
    e32p_obd2_deinit(h);
}

TEST_CASE("obd2: a provider that refuses answers as unsupported", "[e32p_obd2]")
{
    e32p_obd2_handle_t h;
    make_server(k_pids, 4, VIN_DEMO, &h);
    uint8_t resp[E32P_OBD2_RESP_MAX];
    uint16_t n = 0;
    const uint8_t req[] = { 0x01, 0x0C };
    g_refuse = true;
    /* Better a clean "not supported" than a zeroed value a scan tool believes. */
    ask(h, E32P_OBD2_ADDR_PHYSICAL, req, 2, resp, &n);
    TEST_ASSERT_EQUAL(3, n);
    TEST_ASSERT_EQUAL_HEX8(0x7F, resp[0]);
    TEST_ASSERT_EQUAL_HEX8(0x12, resp[2]);
    e32p_obd2_deinit(h);
}

TEST_CASE("obd2: more than one PID per request is refused", "[e32p_obd2]")
{
    e32p_obd2_handle_t h;
    make_server(k_pids, 4, VIN_DEMO, &h);
    uint8_t resp[E32P_OBD2_RESP_MAX];
    uint16_t n = 0;
    const uint8_t req[] = { 0x01, 0x0C, 0x0D };
    ask(h, E32P_OBD2_ADDR_PHYSICAL, req, 3, resp, &n);
    TEST_ASSERT_EQUAL(3, n);
    TEST_ASSERT_EQUAL_HEX8(0x7F, resp[0]);
    TEST_ASSERT_EQUAL_HEX8(0x13, resp[2]);   /* wrong length, not a partial answer */
    e32p_obd2_deinit(h);
}

/* ================ 12-14: the silence rule and bad services =============== */

TEST_CASE("obd2: an unsupported PID is silent functionally, negative physically",
          "[e32p_obd2]")
{
    e32p_obd2_handle_t h;
    make_server(k_pids, 4, VIN_DEMO, &h);
    uint8_t resp[E32P_OBD2_RESP_MAX];
    uint16_t n = 0;
    const uint8_t req[] = { 0x01, 0x42 };

    /* ISO 15765-4: a broadcast must not draw an error from every ECU on the bus. */
    ask(h, E32P_OBD2_ADDR_FUNCTIONAL, req, 2, resp, &n);
    TEST_ASSERT_EQUAL(0, n);

    ask(h, E32P_OBD2_ADDR_PHYSICAL, req, 2, resp, &n);
    TEST_ASSERT_EQUAL(3, n);
    TEST_ASSERT_EQUAL_HEX8(0x7F, resp[0]);
    TEST_ASSERT_EQUAL_HEX8(0x01, resp[1]);
    TEST_ASSERT_EQUAL_HEX8(0x12, resp[2]);
    e32p_obd2_deinit(h);
}

TEST_CASE("obd2: a supported PID answers a functional request too", "[e32p_obd2]")
{
    e32p_obd2_handle_t h;
    make_server(k_pids, 4, VIN_DEMO, &h);
    uint8_t resp[E32P_OBD2_RESP_MAX];
    uint16_t n = 0;
    const uint8_t req[] = { 0x01, 0x0D };
    ask(h, E32P_OBD2_ADDR_FUNCTIONAL, req, 2, resp, &n);
    TEST_ASSERT_EQUAL(3, n);
    TEST_ASSERT_EQUAL_HEX8(0x41, resp[0]);
    TEST_ASSERT_EQUAL_HEX8(88, resp[2]);
    e32p_obd2_deinit(h);
}

TEST_CASE("obd2: an unknown service is refused by number", "[e32p_obd2]")
{
    e32p_obd2_handle_t h;
    make_server(k_pids, 4, VIN_DEMO, &h);
    uint8_t resp[E32P_OBD2_RESP_MAX];
    uint16_t n = 0;
    const uint8_t req[] = { 0x11, 0x00 };
    ask(h, E32P_OBD2_ADDR_PHYSICAL, req, 2, resp, &n);
    TEST_ASSERT_EQUAL(3, n);
    TEST_ASSERT_EQUAL_HEX8(0x7F, resp[0]);
    TEST_ASSERT_EQUAL_HEX8(0x11, resp[1]);   /* the service asked for */
    TEST_ASSERT_EQUAL_HEX8(0x11, resp[2]);   /* serviceNotSupported   */
    ask(h, E32P_OBD2_ADDR_FUNCTIONAL, req, 2, resp, &n);
    TEST_ASSERT_EQUAL(0, n);
    e32p_obd2_deinit(h);
}

/* ===================== 15-17: mode 0x09 vehicle info ===================== */

TEST_CASE("obd2: the VIN comes back as one 20-byte response", "[e32p_obd2]")
{
    e32p_obd2_handle_t h;
    make_server(k_pids, 4, VIN_DEMO, &h);
    uint8_t resp[E32P_OBD2_RESP_MAX];
    uint16_t n = 0;
    const uint8_t req[] = { 0x09, 0x02 };
    ask(h, E32P_OBD2_ADDR_PHYSICAL, req, 2, resp, &n);
    TEST_ASSERT_EQUAL(20, n);
    TEST_ASSERT_EQUAL_HEX8(0x49, resp[0]);
    TEST_ASSERT_EQUAL_HEX8(0x02, resp[1]);
    TEST_ASSERT_EQUAL_HEX8(0x01, resp[2]);   /* one data item */
    TEST_ASSERT_EQUAL_HEX8_ARRAY(VIN_DEMO, &resp[3], E32P_OBD2_VIN_LEN);
    e32p_obd2_deinit(h);
}

TEST_CASE("obd2: mode 0x09 PID 0x00 advertises the VIN", "[e32p_obd2]")
{
    e32p_obd2_handle_t h;
    make_server(k_pids, 4, VIN_DEMO, &h);
    uint8_t resp[E32P_OBD2_RESP_MAX];
    uint16_t n = 0;
    const uint8_t req[] = { 0x09, 0x00 };
    ask(h, E32P_OBD2_ADDR_PHYSICAL, req, 2, resp, &n);
    TEST_ASSERT_EQUAL(6, n);
    TEST_ASSERT_EQUAL_HEX8(0x49, resp[0]);
    TEST_ASSERT_EQUAL_HEX8(0x40, resp[2]);   /* PID 0x02 -> byte0 bit6 */
    e32p_obd2_deinit(h);
}

/* ==================== 18-24: trouble codes and clearing ================== */

TEST_CASE("obd2: a stored code comes back in mode 0x03", "[e32p_obd2]")
{
    e32p_obd2_handle_t h;
    make_server(k_pids, 4, VIN_DEMO, &h);
    uint8_t resp[E32P_OBD2_RESP_MAX];
    uint16_t n = 0;
    TEST_ASSERT_EQUAL(ESP_OK, e32p_obd2_dtc_set(h, 0x0133u, true));   /* P0133 */

    const uint8_t req[] = { 0x03 };
    ask(h, E32P_OBD2_ADDR_PHYSICAL, req, 1, resp, &n);
    TEST_ASSERT_EQUAL(4, n);
    TEST_ASSERT_EQUAL_HEX8(0x43, resp[0]);
    TEST_ASSERT_EQUAL_HEX8(0x01, resp[1]);   /* one code, not two bytes */
    TEST_ASSERT_EQUAL_HEX8(0x01, resp[2]);
    TEST_ASSERT_EQUAL_HEX8(0x33, resp[3]);
    e32p_obd2_deinit(h);
}

TEST_CASE("obd2: pending and stored codes are separate lists", "[e32p_obd2]")
{
    e32p_obd2_handle_t h;
    make_server(k_pids, 4, VIN_DEMO, &h);
    uint8_t resp[E32P_OBD2_RESP_MAX];
    uint16_t n = 0;
    TEST_ASSERT_EQUAL(ESP_OK, e32p_obd2_dtc_set(h, 0x0420u, false));  /* pending */
    TEST_ASSERT_EQUAL(1, e32p_obd2_dtc_count(h, false));
    TEST_ASSERT_EQUAL(0, e32p_obd2_dtc_count(h, true));

    const uint8_t pending[] = { 0x07 };
    ask(h, E32P_OBD2_ADDR_PHYSICAL, pending, 1, resp, &n);
    TEST_ASSERT_EQUAL(4, n);
    TEST_ASSERT_EQUAL_HEX8(0x47, resp[0]);
    TEST_ASSERT_EQUAL_HEX8(0x04, resp[2]);
    TEST_ASSERT_EQUAL_HEX8(0x20, resp[3]);

    const uint8_t stored[] = { 0x03 };
    ask(h, E32P_OBD2_ADDR_PHYSICAL, stored, 1, resp, &n);
    TEST_ASSERT_EQUAL(2, n);                 /* "no stored codes" is still an answer */
    TEST_ASSERT_EQUAL_HEX8(0x43, resp[0]);
    TEST_ASSERT_EQUAL_HEX8(0x00, resp[1]);
    e32p_obd2_deinit(h);
}

TEST_CASE("obd2: dtc_get lists every code in order, then NOT_FOUND", "[e32p_obd2]")
{
    e32p_obd2_handle_t h;
    make_server(k_pids, 4, VIN_DEMO, &h);
    TEST_ASSERT_EQUAL(ESP_OK, e32p_obd2_dtc_set(h, 0x0133u, true));
    TEST_ASSERT_EQUAL(ESP_OK, e32p_obd2_dtc_set(h, 0x0420u, false));

    uint16_t dtc = 0;
    bool confirmed = false;
    TEST_ASSERT_EQUAL(ESP_OK, e32p_obd2_dtc_get(h, 0, &dtc, &confirmed));
    TEST_ASSERT_EQUAL_HEX16(0x0133, dtc);
    TEST_ASSERT_TRUE(confirmed);
    TEST_ASSERT_EQUAL(ESP_OK, e32p_obd2_dtc_get(h, 1, &dtc, &confirmed));
    TEST_ASSERT_EQUAL_HEX16(0x0420, dtc);
    TEST_ASSERT_FALSE(confirmed);
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, e32p_obd2_dtc_get(h, 2, &dtc, &confirmed));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_obd2_dtc_get(h, 0, NULL, &confirmed));

    TEST_ASSERT_EQUAL(ESP_OK, e32p_obd2_dtc_clear(h));
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, e32p_obd2_dtc_get(h, 0, &dtc, &confirmed));
    e32p_obd2_deinit(h);
}

TEST_CASE("obd2: the count byte counts codes, not bytes", "[e32p_obd2]")
{
    e32p_obd2_handle_t h;
    make_server(k_pids, 4, VIN_DEMO, &h);
    uint8_t resp[E32P_OBD2_RESP_MAX];
    uint16_t n = 0;
    TEST_ASSERT_EQUAL(ESP_OK, e32p_obd2_dtc_set(h, 0x0133u, true));
    TEST_ASSERT_EQUAL(ESP_OK, e32p_obd2_dtc_set(h, 0xC123u, true));

    const uint8_t req[] = { 0x03 };
    ask(h, E32P_OBD2_ADDR_PHYSICAL, req, 1, resp, &n);
    TEST_ASSERT_EQUAL(6, n);
    TEST_ASSERT_EQUAL_HEX8(0x02, resp[1]);
    TEST_ASSERT_EQUAL_HEX8(0xC1, resp[4]);
    TEST_ASSERT_EQUAL_HEX8(0x23, resp[5]);
    e32p_obd2_deinit(h);
}

TEST_CASE("obd2: mode 0x04 clears every list", "[e32p_obd2]")
{
    e32p_obd2_handle_t h;
    make_server(k_pids, 4, VIN_DEMO, &h);
    uint8_t resp[E32P_OBD2_RESP_MAX];
    uint16_t n = 0;
    TEST_ASSERT_EQUAL(ESP_OK, e32p_obd2_dtc_set(h, 0x0133u, true));
    TEST_ASSERT_EQUAL(ESP_OK, e32p_obd2_dtc_set(h, 0x0420u, false));

    const uint8_t clear[] = { 0x04 };
    ask(h, E32P_OBD2_ADDR_PHYSICAL, clear, 1, resp, &n);
    TEST_ASSERT_EQUAL(1, n);
    TEST_ASSERT_EQUAL_HEX8(0x44, resp[0]);
    TEST_ASSERT_EQUAL(0, e32p_obd2_dtc_count(h, true));
    TEST_ASSERT_EQUAL(0, e32p_obd2_dtc_count(h, false));

    const uint8_t stored[] = { 0x03 };
    ask(h, E32P_OBD2_ADDR_PHYSICAL, stored, 1, resp, &n);
    TEST_ASSERT_EQUAL(2, n);
    TEST_ASSERT_EQUAL_HEX8(0x00, resp[1]);

    /* The freeze frame goes with them: it describes a fault that is now cleared. */
    const uint8_t frz[] = { 0x02, 0x0C, 0x00 };
    ask(h, E32P_OBD2_ADDR_PHYSICAL, frz, 3, resp, &n);
    TEST_ASSERT_EQUAL(3, n);
    TEST_ASSERT_EQUAL_HEX8(0x12, resp[2]);
    e32p_obd2_deinit(h);
}

TEST_CASE("obd2: a request with a payload where none belongs is refused", "[e32p_obd2]")
{
    e32p_obd2_handle_t h;
    make_server(k_pids, 4, VIN_DEMO, &h);
    uint8_t resp[E32P_OBD2_RESP_MAX];
    uint16_t n = 0;
    const uint8_t req[] = { 0x03, 0x01 };
    ask(h, E32P_OBD2_ADDR_PHYSICAL, req, 2, resp, &n);
    TEST_ASSERT_EQUAL(3, n);
    TEST_ASSERT_EQUAL_HEX8(0x13, resp[2]);
    e32p_obd2_deinit(h);
}

TEST_CASE("obd2: the store has a limit and says so", "[e32p_obd2]")
{
    e32p_obd2_handle_t h;
    make_server(k_pids, 4, VIN_DEMO, &h);
    uint8_t resp[E32P_OBD2_RESP_MAX];
    uint16_t n = 0;
    int stored = 0;
    for (uint16_t code = 0x0100u; code < 0x0200u; code++) {
        if (e32p_obd2_dtc_set(h, code, true) != ESP_OK) {
            break;
        }
        stored++;
    }
    TEST_ASSERT_GREATER_THAN(0, stored);
    TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM, e32p_obd2_dtc_set(h, 0x0999u, true));

    const uint8_t req[] = { 0x03 };
    ask(h, E32P_OBD2_ADDR_PHYSICAL, req, 1, resp, &n);
    TEST_ASSERT_EQUAL(2 + 2 * stored, n);     /* the full list still fits the buffer */
    TEST_ASSERT_EQUAL(stored, resp[1]);
    e32p_obd2_deinit(h);
}

/* ======================= 25-27: the freeze frame ========================= */

TEST_CASE("obd2: the freeze frame keeps the values from when the code appeared",
          "[e32p_obd2]")
{
    e32p_obd2_handle_t h;
    make_server(k_pids, 4, VIN_DEMO, &h);
    uint8_t resp[E32P_OBD2_RESP_MAX];
    uint16_t n = 0;
    TEST_ASSERT_EQUAL(ESP_OK, e32p_obd2_dtc_set(h, 0x0133u, true));

    g_rpm_hi = 0x00u;                         /* the engine moves on */
    g_rpm_lo = 0x64u;

    const uint8_t live[] = { 0x01, 0x0C };
    ask(h, E32P_OBD2_ADDR_PHYSICAL, live, 2, resp, &n);
    TEST_ASSERT_EQUAL_HEX8(0x00, resp[2]);
    TEST_ASSERT_EQUAL_HEX8(0x64, resp[3]);

    const uint8_t frozen[] = { 0x02, 0x0C, 0x00 };
    ask(h, E32P_OBD2_ADDR_PHYSICAL, frozen, 3, resp, &n);
    TEST_ASSERT_EQUAL(5, n);
    TEST_ASSERT_EQUAL_HEX8(0x42, resp[0]);
    TEST_ASSERT_EQUAL_HEX8(0x0C, resp[1]);
    TEST_ASSERT_EQUAL_HEX8(0x00, resp[2]);    /* frame 0 */
    TEST_ASSERT_EQUAL_HEX8(0x1A, resp[3]);    /* the value at the time of the fault */
    TEST_ASSERT_EQUAL_HEX8(0xF8, resp[4]);
    e32p_obd2_deinit(h);
}

TEST_CASE("obd2: freeze frame PID 0x02 is the code that froze it", "[e32p_obd2]")
{
    e32p_obd2_handle_t h;
    make_server(k_pids, 4, VIN_DEMO, &h);
    uint8_t resp[E32P_OBD2_RESP_MAX];
    uint16_t n = 0;
    TEST_ASSERT_EQUAL(ESP_OK, e32p_obd2_dtc_set(h, 0x0133u, true));
    TEST_ASSERT_EQUAL(ESP_OK, e32p_obd2_dtc_set(h, 0x0420u, true));   /* not this one */

    const uint8_t req[] = { 0x02, 0x02, 0x00 };
    ask(h, E32P_OBD2_ADDR_PHYSICAL, req, 3, resp, &n);
    TEST_ASSERT_EQUAL(5, n);
    TEST_ASSERT_EQUAL_HEX8(0x01, resp[3]);
    TEST_ASSERT_EQUAL_HEX8(0x33, resp[4]);
    e32p_obd2_deinit(h);
}

TEST_CASE("obd2: only frame 0 exists, and only after a code", "[e32p_obd2]")
{
    e32p_obd2_handle_t h;
    make_server(k_pids, 4, VIN_DEMO, &h);
    uint8_t resp[E32P_OBD2_RESP_MAX];
    uint16_t n = 0;

    const uint8_t before[] = { 0x02, 0x0C, 0x00 };
    ask(h, E32P_OBD2_ADDR_PHYSICAL, before, 3, resp, &n);
    TEST_ASSERT_EQUAL(3, n);
    TEST_ASSERT_EQUAL_HEX8(0x12, resp[2]);
    ask(h, E32P_OBD2_ADDR_FUNCTIONAL, before, 3, resp, &n);
    TEST_ASSERT_EQUAL(0, n);

    TEST_ASSERT_EQUAL(ESP_OK, e32p_obd2_dtc_set(h, 0x0133u, true));
    const uint8_t frame1[] = { 0x02, 0x0C, 0x01 };
    ask(h, E32P_OBD2_ADDR_PHYSICAL, frame1, 3, resp, &n);
    TEST_ASSERT_EQUAL(3, n);
    TEST_ASSERT_EQUAL_HEX8(0x12, resp[2]);
    e32p_obd2_deinit(h);
}

/* ========================== 28: persistence ============================== */

TEST_CASE("obd2: codes survive a restart", "[e32p_obd2]")
{
    /* Runs against real NVS on the target. It erases its own key at the end, so
       running the suite twice gives the same result. */
    TEST_ASSERT_EQUAL(ESP_OK, e32p_nvs_init());
    e32p_obd2_config_t cfg = {
        .pids = k_pids, .pid_count = 4, .on_read = read_pid, .user = NULL,
        .vin = VIN_DEMO, .obd_standard = 0x06u, .nvs_namespace = DTC_NS,
    };
    e32p_obd2_handle_t h = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, e32p_obd2_init(&cfg, &h));
    TEST_ASSERT_EQUAL(ESP_OK, e32p_obd2_dtc_clear(h));                /* known state */
    TEST_ASSERT_EQUAL(ESP_OK, e32p_obd2_dtc_set(h, 0x0133u, true));
    TEST_ASSERT_EQUAL(ESP_OK, e32p_obd2_dtc_set(h, 0x0420u, false));
    e32p_obd2_deinit(h);

    h = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, e32p_obd2_init(&cfg, &h));              /* "reboot" */
    TEST_ASSERT_EQUAL(1, e32p_obd2_dtc_count(h, true));
    TEST_ASSERT_EQUAL(1, e32p_obd2_dtc_count(h, false));

    uint8_t resp[E32P_OBD2_RESP_MAX];
    uint16_t n = 0;
    const uint8_t req[] = { 0x03 };
    ask(h, E32P_OBD2_ADDR_PHYSICAL, req, 1, resp, &n);
    TEST_ASSERT_EQUAL(4, n);
    TEST_ASSERT_EQUAL_HEX8(0x01, resp[2]);
    TEST_ASSERT_EQUAL_HEX8(0x33, resp[3]);

    /* The freeze frame is deliberately not persisted: it described live values. */
    const uint8_t frz[] = { 0x02, 0x0C, 0x00 };
    ask(h, E32P_OBD2_ADDR_PHYSICAL, frz, 3, resp, &n);
    TEST_ASSERT_EQUAL(3, n);
    TEST_ASSERT_EQUAL_HEX8(0x12, resp[2]);

    TEST_ASSERT_EQUAL(ESP_OK, e32p_obd2_dtc_clear(h));
    e32p_obd2_deinit(h);
}

TEST_CASE("obd2: without a VIN, mode 0x09 is unsupported", "[e32p_obd2]")
{
    e32p_obd2_handle_t h;
    make_server(k_pids, 4, NULL, &h);
    uint8_t resp[E32P_OBD2_RESP_MAX];
    uint16_t n = 0;
    const uint8_t vin[] = { 0x09, 0x02 };
    ask(h, E32P_OBD2_ADDR_PHYSICAL, vin, 2, resp, &n);
    TEST_ASSERT_EQUAL(3, n);
    TEST_ASSERT_EQUAL_HEX8(0x12, resp[2]);
    const uint8_t sup[] = { 0x09, 0x00 };
    ask(h, E32P_OBD2_ADDR_PHYSICAL, sup, 2, resp, &n);
    TEST_ASSERT_EQUAL(3, n);
    TEST_ASSERT_EQUAL_HEX8(0x12, resp[2]);
    ask(h, E32P_OBD2_ADDR_FUNCTIONAL, sup, 2, resp, &n);
    TEST_ASSERT_EQUAL(0, n);
    e32p_obd2_deinit(h);
}
