/**
 * @file hil_diag.c
 * @brief Diagnostics HIL group: the ISO-TP tests the DUT runs, and the ISO-TP
 *        peer the COMPANION runs inside its CAN loop.
 *
 * Address plan (tester SA 0xF1):
 *
 *   ECU   11-bit req -> resp   what answers
 *   0x10  0x7E0 -> 0x7E8       COMPANION OBD-II server (also broadcast 0x7DF)
 *   0x11  0x7E1 -> 0x7E9       COMPANION UDS server (also broadcast 0x7DF)
 *   0x12  0x7E2 -> 0x7EA       COMPANION echo peer
 *   0x13  0x7E3 -> 0x7EB       COMPANION echo peer that pauses the sender with FC.WAIT
 *   0x14  0x7E4 -> 0x7EC       COMPANION that starts a response and abandons it (N_Cr)
 *   0x15  0x7E5 -> 0x7ED       DUT only: two channels looped through self-reception
 *
 * In normal-fixed 29-bit builds the same ECU addresses compose 0x18DA{TA}{SA}.
 *
 * The COMPANION peer drives the pure engine directly, not the TWAI link. The
 * companion's CAN task in hil_can.c already owns e32p_twai_receive() and routes
 * diagnostics frames here; a second reader of the one RX queue would race it.
 */
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "mbedtls/md.h"
#include "e32p_time.h"
#include "e32p_twai.h"
#include "e32p_isotp.h"
#include "e32p_isotp_twai.h"
#include "e32p_nvs.h"
#include "e32p_obd2.h"
#include "e32p_uds.h"
#include "hil_agent.h"
#include "hil_can.h"
#include "hil_pins.h"
#include "hil_diag.h"

#define TESTER_SA    0xF1u
#define ECU_OBD      0x10u    /* OBD-II server: 0x7E0 -> 0x7E8, broadcast 0x7DF */
#define OBD_FUNC_TA  0x33u    /* 29-bit functional target address           */
#define OBD_FUNC_11  0x7DFu   /* 11-bit functional request ID               */
#define OBD_MAX_MSG  128u     /* longest OBD response is 68 bytes           */
#define ECU_UDS      0x11u    /* UDS server: 0x7E1 -> 0x7E9, broadcast 0x7DF    */
#define UDS_FUNC_MSG 64u      /* broadcasts are single frames               */
#define ECU_ECHO     0x12u
#define ECU_FCWAIT   0x13u
#define ECU_NCR      0x14u
#define ECU_SELF     0x15u
#define REQ_ID(ecu)  (0x7E0u + ((ecu) - 0x10u))
#define RESP_ID(ecu) (0x7E8u + ((ecu) - 0x10u))

#define XFER_MAX     1024u    /* largest payload any test sends          */
#define XFER_MS      5000u    /* blocking send/recv budget per direction  */
#define POLL_MS      10u      /* test-task wait granularity               */
E32P_ASSERT_BLOCKING_MS(POLL_MS);
_Static_assert(XFER_MAX <= CONFIG_E32P_ISOTP_MAX_MSG_BYTES,
               "HIL ISO-TP payloads exceed CONFIG_E32P_ISOTP_MAX_MSG_BYTES");

#if defined(CONFIG_E32P_ISOTP_ADDRESSING_NORMAL_FIXED)
#  define ADDR_TAG "normfixed"
#else
#  define ADDR_TAG "norm11"
#endif
/* Every verdict names the build it ran on. The host asserts it matches
   --can-bitrate / --isotp-addr, so a wrong binary fails loudly. */
#define BUILD_FMT " rate=%lu addr=" ADDR_TAG
#define BUILD_ARG ((unsigned long)HIL_CAN_BITRATE)

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static uint32_t get32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

/** @brief CRC-32 (IEEE, as zlib.crc32) - one step; start at 0xFFFFFFFF, invert at the end. */
static uint32_t crc32_step(uint32_t crc, const uint8_t *d, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++) {
        crc ^= d[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return crc;
}

/* HIL TEST SECRET. A test fixture, not a key: it is in source so that the DUT, the
   tester, can compute keys, and it guards nothing. The companion writes it to NVS
   at boot and its server reads it back from there, the path a product's secret
   takes. A product provisions its own per-device secret instead (example 33).
   Item 4 of the security audit in docs/SECURITY_HARDENING.md. */
#define UDS_TEST_SECRET "HIL TEST SECRET - NOT A REAL KEY"
#define UDS_SECRET_LEN  (sizeof(UDS_TEST_SECRET) - 1u)

/** @brief key = HMAC-SHA256(secret, seed), first @p len bytes - the rule example 33 uses. */
static bool uds_key_for(const uint8_t *secret, const uint8_t *seed, uint8_t len, uint8_t *key)
{
    uint8_t mac[32];
    if (len > sizeof(mac) ||
        mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), secret, UDS_SECRET_LEN,
                        seed, len, mac) != 0) {
        return false;
    }
    memcpy(key, mac, len);
    return true;
}

/** @brief One address pair, from the tester's side (DUT) or the ECU's (companion). */
static void cfg_for(e32p_isotp_config_t *c, uint8_t ecu, bool tester)
{
    if (tester) {
        e32p_isotp_config_defaults(c, REQ_ID(ecu), RESP_ID(ecu), TESTER_SA, ecu, false);
    } else {
        e32p_isotp_config_defaults(c, RESP_ID(ecu), REQ_ID(ecu), ecu, TESTER_SA, false);
    }
}

/** @brief The CAN ID an ECU channel transmits on, in either addressing mode. */
static uint32_t ecu_tx_id(uint8_t ecu)
{
#if defined(CONFIG_E32P_ISOTP_ADDRESSING_NORMAL_FIXED)
    return e32p_isotp_id_normal_fixed(ecu, TESTER_SA, false);
#else
    return RESP_ID(ecu);
#endif
}

/* ============================ COMPANION peer ============================ */

typedef enum {
    PEER_ECHO = 0, PEER_FCWAIT, PEER_NCR,   /* Phase 1 ISO-TP peers           */
    PEER_OBD_PHYS, PEER_OBD_FUNC,           /* Phase 2 OBD-II server channels  */
    PEER_UDS_PHYS, PEER_UDS_FUNC,           /* Phase 3 UDS server channels     */
    PEER_N
} peer_kind_t;
#define PEER_ISOTP_N PEER_OBD_PHYS          /* the peers built from the spec table */

/* DTC seed handshake. The request comes from the command task, but only the CAN
   task may touch the server, so the command sets REQ and the CAN task does the
   work on its next pass and answers DONE. */
#define SEED_IDLE 0
#define SEED_REQ  1
#define SEED_DONE 2

static struct {
    bool                inited;
    bool                failed;
    e32p_isotp_handle_t ch[PEER_N];
    e32p_obd2_handle_t  obd;
    int                 seed;               /* SEED_*, accessed with __atomic */
    esp_err_t           seed_err;
} s_peer;

static esp_err_t peer_sink(const e32p_twai_message_t *f, void *u)
{
    (void)u;
    return e32p_twai_transmit(f, 0);     /* full queue = back-pressure, engine retries */
}

/** @brief A request finished reassembling on one of the companion's channels.
 *         Runs inside the companion's CAN loop - one @EVT per payload, never per
 *         frame, so the serial write cannot starve the RX queue. */
static void peer_on_rx(const uint8_t *d, uint16_t len, void *u)
{
    const peer_kind_t k = (peer_kind_t)(intptr_t)u;

    if (k == PEER_NCR) {
        /* Start a 100-byte response and abandon it: one First Frame, then no
           Consecutive Frames. The DUT must discard the session on N_Cr. */
        e32p_twai_message_t ff = {
            .id = ecu_tx_id(ECU_NCR),
            .extended = e32p_isotp_is_extended(s_peer.ch[PEER_NCR]),
            .len = 8,
            .data = { 0x10, 0x64, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA },
        };
        (void)e32p_twai_transmit(&ff, 0);
        hil_emit("@EVT ISOTP ncr ff-sent");
        return;
    }
    const esp_err_t err = e32p_isotp_send(s_peer.ch[k], d, len);
    hil_emit("@EVT ISOTP rx ch=%s len=%u echo=%s", k == PEER_ECHO ? "echo" : "fcwait",
             len, err == ESP_OK ? "ok" : esp_err_to_name(err));
}

/* ---- OBD-II server: fixed values, so the DUT's assertions can be exact ---- */

#define OBD_VIN "E32PHIL0000000001"

static const e32p_obd2_pid_t k_obd_pids[] = {
    { 0x05, 1 },   /* coolant 90 degC -> A = 130 */
    { 0x0C, 2 },   /* 1726 rpm        -> 0x1AF8  */
    { 0x0D, 1 },   /* 88 km/h                    */
    { 0x1C, 1 },   /* OBD standard               */
};

static esp_err_t obd_read(uint8_t pid, uint8_t *out, uint8_t len, void *u)
{
    (void)len;
    (void)u;
    switch (pid) {
    case 0x05: out[0] = 130u;                   return ESP_OK;
    case 0x0C: out[0] = 0x1Au; out[1] = 0xF8u;  return ESP_OK;
    case 0x0D: out[0] = 88u;                    return ESP_OK;
    default:                                    return ESP_FAIL;
    }
}

/** @brief A request reassembled on an OBD channel. Only the physical channel
 *         transmits: a broadcast is answered from the ECU's own ID, as the
 *         component's glue does. */
static void obd_on_rx(const uint8_t *d, uint16_t len, void *u)
{
    const e32p_obd2_addressing_t a = (e32p_obd2_addressing_t)(intptr_t)u;
    uint8_t resp[E32P_OBD2_RESP_MAX];
    uint16_t n = 0;
    if (e32p_obd2_request(s_peer.obd, a, d, len, resp, sizeof(resp), &n) != ESP_OK || n == 0u) {
        return;
    }
    if (!e32p_isotp_tx_busy(s_peer.ch[PEER_OBD_PHYS])) {
        (void)e32p_isotp_send(s_peer.ch[PEER_OBD_PHYS], resp, n);
    }
}

static esp_err_t obd_peer_init(void)
{
    const e32p_obd2_config_t sc = {
        .pids = k_obd_pids, .pid_count = sizeof(k_obd_pids) / sizeof(k_obd_pids[0]),
        .on_read = obd_read, .vin = OBD_VIN, .obd_standard = 0x06u,
        .nvs_namespace = NULL,       /* RAM store: persistence is proven in Unity */
    };
    esp_err_t err = e32p_obd2_init(&sc, &s_peer.obd);
    if (err != ESP_OK) {
        return err;
    }

    e32p_isotp_config_t c;
    cfg_for(&c, ECU_OBD, false);                     /* 0x7E0 in, 0x7E8 out */
    c.max_msg_bytes = OBD_MAX_MSG;
    c.tx = peer_sink;
    c.on_rx = obd_on_rx;
    c.user = (void *)(intptr_t)E32P_OBD2_ADDR_PHYSICAL;
    err = e32p_isotp_init(&c, &s_peer.ch[PEER_OBD_PHYS]);
    if (err != ESP_OK) {
        return err;
    }

    /* Receive-only. 29-bit: the engine listens on 0x18DB<source><target>, and a
       tester broadcasts on 0x18DB33F1, so source is 0x33 and target the tester. */
    e32p_isotp_config_defaults(&c, RESP_ID(ECU_OBD), OBD_FUNC_11, OBD_FUNC_TA, TESTER_SA, true);
    c.max_msg_bytes = OBD_MAX_MSG;
    c.tx = peer_sink;
    c.on_rx = obd_on_rx;
    c.user = (void *)(intptr_t)E32P_OBD2_ADDR_FUNCTIONAL;
    return e32p_isotp_init(&c, &s_peer.ch[PEER_OBD_FUNC]);
}

/* ---- UDS server: the same ECU data, its trouble codes shared with OBD-II ---- */

#define UDS_NVS_NS        "hil_uds"
#define UDS_NVS_SECRET    "secret"
#define UDS_NVS_SEC_FAIL  "sec_fail"   /* i32: 1 = the last security access ended in a wrong key */
#define UDS_DID_VIN       0xF190u
#define UDS_DID_SETTING   0x0100u      /* RAM only: back to its boot value after a reset */
#define UDS_SETTING_BOOT  0x00000001u
#define UDS_RID_INJECT    0x0201u      /* stores P0217 in the OBD-II store             */
#define UDS_RID_ERASE     0xFF00u
#define UDS_RID_CHECK     0xFF01u
#define UDS_DTC_INJECTED  0x0217u      /* P0217, engine over temperature               */
#define UDS_ERASE_MS      2000u        /* long enough that the server must send 0x78   */
#define UDS_ERASE_MAX_S   20u          /* longest erase a tester may ask for           */
#define UDS_RESET_WAIT_MS 100u         /* lets the 51 01 leave the TX queue first      */
#define UDS_SINK_BYTES    4096u
#define UDS_ST_PENDING    0x04u        /* ISO 14229-1 DTC status bits                  */
#define UDS_ST_CONFIRMED  0x08u

/* Touched only by the companion's CAN task, like everything else in the peer. */
static struct {
    e32p_uds_handle_t h;
    uint8_t           secret[UDS_SECRET_LEN];
    uint32_t          setting;
    uint8_t          *ram;              /* the download sink: RAM, never flash */
    uint32_t          ram_len;
    uint32_t          ram_crc;
    bool              erasing;
    bool              erased;
    uint32_t          erase_done_ms;
    bool              reset_armed;
    uint32_t          reset_at_ms;
} s_uds;

static esp_err_t uds_send(const uint8_t *d, uint16_t len, void *u)
{
    (void)u;
    return e32p_isotp_send(s_peer.ch[PEER_UDS_PHYS], d, len);   /* copies: d is only valid now */
}

/** @brief A request reassembled on a UDS channel. Answers always leave on the
 *         physical channel, a broadcast's too. */
static void uds_on_rx(const uint8_t *d, uint16_t len, void *u)
{
    (void)e32p_uds_request(s_uds.h, (e32p_uds_addressing_t)(intptr_t)u, d, len, now_ms());
}

static const e32p_uds_did_t k_uds_dids[] = {
    { UDS_DID_VIN,     E32P_OBD2_VIN_LEN, E32P_UDS_IN_ANY, 0u,                   false },
    { UDS_DID_SETTING, 4u,                E32P_UDS_IN_ANY, E32P_UDS_IN_EXTENDED, true  },
};

static e32p_uds_nrc_t uds_did_read(uint16_t did, uint8_t *out, uint16_t len, void *u)
{
    (void)u;
    if (did == UDS_DID_VIN) {
        memcpy(out, OBD_VIN, len);         /* one ECU: the VIN OBD-II 09 02 reports */
    } else {
        put32(out, s_uds.setting);
    }
    return E32P_UDS_NRC_OK;
}

static e32p_uds_nrc_t uds_did_write(uint16_t did, const uint8_t *data, uint16_t len, void *u)
{
    (void)did;                             /* the table makes only 0x0100 writable */
    (void)len;
    (void)u;
    s_uds.setting = get32(data);
    return E32P_UDS_NRC_OK;
}

static e32p_uds_nrc_t uds_make_seed(uint8_t level, uint8_t *seed, uint8_t len, void *u)
{
    (void)level;
    (void)u;
    bool zero = true;
    while (zero) {                         /* all zero would mean "already unlocked" */
        esp_fill_random(seed, len);
        for (uint8_t i = 0; i < len; i++) {
            zero = zero && seed[i] == 0u;
        }
    }
    return E32P_UDS_NRC_OK;
}

static bool uds_check_key(uint8_t level, const uint8_t *seed, const uint8_t *key, uint8_t len,
                          void *u)
{
    (void)u;
    uint8_t want[E32P_UDS_SECURITY_LEN];
    if (level != 1u || len != sizeof(want) || !uds_key_for(s_uds.secret, seed, len, want)) {
        return false;
    }
    uint8_t diff = 0u;                     /* constant time: no early exit */
    for (uint8_t i = 0; i < len; i++) {
        diff |= (uint8_t)(want[i] ^ key[i]);
    }
    return diff == 0u;
}

/* The trouble codes are the OBD-II server's store, seen through UDS (decision 3):
   a code set or cleared through either view is the same code. */
static uint16_t uds_dtc_count(void *u)
{
    (void)u;
    return (uint16_t)(e32p_obd2_dtc_count(s_peer.obd, true) + e32p_obd2_dtc_count(s_peer.obd, false));
}

static bool uds_dtc_at(uint16_t index, uint32_t *dtc, uint8_t *status, void *u)
{
    (void)u;
    uint16_t code = 0;
    bool confirmed = false;
    if (index > UINT8_MAX || e32p_obd2_dtc_get(s_peer.obd, (uint8_t)index, &code, &confirmed) != ESP_OK) {
        return false;
    }
    *dtc = (uint32_t)code << 8;            /* ISO 15031-6 code, failure type byte 00 */
    *status = confirmed ? UDS_ST_CONFIRMED : UDS_ST_PENDING;
    return true;
}

static e32p_uds_nrc_t uds_dtc_clear(uint32_t group, void *u)
{
    (void)group;                           /* the server only passes FFFFFF: all */
    (void)u;
    return (e32p_obd2_dtc_clear(s_peer.obd) == ESP_OK) ? E32P_UDS_NRC_OK
                                                       : E32P_UDS_NRC_PROGRAMMING_FAILURE;
}

static const e32p_uds_dtc_provider_t k_uds_dtc = {
    .availability_mask = UDS_ST_PENDING | UDS_ST_CONFIRMED,   /* all the OBD-II store knows */
    .format = E32P_UDS_DTC_FORMAT_ISO15031,
    .count = uds_dtc_count,
    .at = uds_dtc_at,
    .clear = uds_dtc_clear,
};

static e32p_uds_nrc_t uds_sink_begin(uint32_t size, void *u)
{
    (void)u;
    if (s_uds.erasing || !s_uds.erased) {
        return E32P_UDS_NRC_CONDITIONS_NOT_CORRECT;   /* erase first: 31 01 FF00 */
    }
    if (size > UDS_SINK_BYTES) {
        return E32P_UDS_NRC_DOWNLOAD_NOT_ACCEPTED;
    }
    s_uds.ram_len = 0u;
    return E32P_UDS_NRC_OK;
}

static e32p_uds_nrc_t uds_sink_write(const uint8_t *data, uint16_t len, void *u)
{
    (void)u;                               /* the server keeps the byte count in bounds */
    memcpy(&s_uds.ram[s_uds.ram_len], data, len);
    s_uds.ram_len += len;
    return E32P_UDS_NRC_OK;
}

static e32p_uds_nrc_t uds_sink_end(void *u)
{
    (void)u;
    s_uds.ram_crc = ~crc32_step(0xFFFFFFFFu, s_uds.ram, s_uds.ram_len);
    s_uds.erased = false;                  /* the next download needs a fresh erase */
    return E32P_UDS_NRC_OK;
}

static void uds_sink_abort(void *u)
{
    (void)u;
    s_uds.ram_len = 0u;
    s_uds.erased = false;
}

static const e32p_uds_sink_t k_uds_sink = {
    UDS_SINK_BYTES, uds_sink_begin, uds_sink_write, uds_sink_end, uds_sink_abort,
};

static const e32p_uds_routine_t k_uds_routines[] = {
    { UDS_RID_INJECT, E32P_UDS_IN_EXTENDED | E32P_UDS_IN_PROGRAMMING, false },
    { UDS_RID_ERASE,  E32P_UDS_IN_PROGRAMMING,                        true  },
    { UDS_RID_CHECK,  E32P_UDS_IN_PROGRAMMING,                        true  },
};

static e32p_uds_nrc_t uds_routine(uint16_t rid, uint8_t control, const uint8_t *option,
                                  uint16_t option_len, uint8_t *status, uint16_t status_cap,
                                  uint16_t *status_len, void *u)
{
    (void)u;
    if (control != 0x01u) {
        return E32P_UDS_NRC_SEQUENCE_ERROR;   /* 03: none of these routines keeps results */
    }
    switch (rid) {
    case UDS_RID_INJECT:
        if (e32p_obd2_dtc_set(s_peer.obd, UDS_DTC_INJECTED, true) != ESP_OK) {
            return E32P_UDS_NRC_CONDITIONS_NOT_CORRECT;
        }
        status[0] = 0x00u;
        *status_len = 1u;
        return E32P_UDS_NRC_OK;
    case UDS_RID_ERASE:
        /* Finishes in uds_service(): the server says "please wait" (0x78) meanwhile.
           An optional 1-byte option sets how long it takes, in seconds: a job longer
           than P2* makes the server repeat its 0x78 (test_uds_canable.py). */
        if (option_len > 1u) {
            return E32P_UDS_NRC_BAD_LENGTH;
        }
        if (option_len == 1u && (option[0] == 0u || option[0] > UDS_ERASE_MAX_S)) {
            return E32P_UDS_NRC_OUT_OF_RANGE;
        }
        if (s_uds.erasing) {
            return E32P_UDS_NRC_CONDITIONS_NOT_CORRECT;
        }
        s_uds.erasing = true;
        s_uds.erased = false;
        s_uds.erase_done_ms = now_ms() + ((option_len == 1u) ? option[0] * 1000u : UDS_ERASE_MS);
        *status_len = 0u;
        return E32P_UDS_NRC_PENDING;
    default:
        /* UDS_RID_CHECK, option = the CRC-32 the tester expects. Status: 00 match or
           01 mismatch, then this ECU's CRC-32. */
        if (option_len != 4u || status_cap < 5u) {
            return E32P_UDS_NRC_BAD_LENGTH;
        }
        status[0] = (get32(option) == s_uds.ram_crc) ? 0x00u : 0x01u;
        put32(&status[1], s_uds.ram_crc);
        *status_len = 5u;
        return E32P_UDS_NRC_OK;
    }
}

static void uds_on_reset(uint8_t type, void *u)
{
    (void)type;
    (void)u;
    /* Not yet: the 51 01 may still sit in the TX queue. uds_service() restarts. */
    s_uds.reset_at_ms = now_ms() + UDS_RESET_WAIT_MS;
    s_uds.reset_armed = true;
}

/** @brief CAN task side of the server: its timers, the erase finishing, the reset. */
static void uds_service(uint32_t now)
{
    if (s_uds.h == NULL) {
        return;
    }
    if (s_uds.erasing && (int32_t)(now - s_uds.erase_done_ms) >= 0) {
        memset(s_uds.ram, 0xFF, UDS_SINK_BYTES);
        s_uds.erasing = false;
        s_uds.erased = true;
        const uint8_t done = 0x00u;
        (void)e32p_uds_complete(s_uds.h, E32P_UDS_NRC_OK, &done, 1u, now);
    }
    (void)e32p_uds_poll(s_uds.h, now);
    if (s_uds.reset_armed && (int32_t)(now - s_uds.reset_at_ms) >= 0) {
        hil_emit("@EVT UDS reset: restarting");
        esp_restart();                     /* a real reset: the host waits for READY */
    }
}

/**
 * @brief The saved "last security access failed" flag, read once when the server starts.
 *        Hook contract: e32p_uds_sec_load_fn.
 */
static bool uds_sec_load(void *u)
{
    (void)u;
    int32_t v = 0;
    const esp_err_t err = e32p_nvs_read_i32(UDS_NVS_NS, UDS_NVS_SEC_FAIL, &v);
    if (e32p_nvs_is_not_found(err)) {
        /* Missing may be answered false only if it can only mean "no access has failed
           since the storage was initialised" (e32p_uds.h). This companion does not meet
           that rule: its secret is a public, compiled-in fixture rewritten at every
           boot, so a wipe leaves a usable secret. A missing flag only follows a fresh
           NVS; that is accepted for a test fixture and is not a pattern for products. */
        return false;
    }
    return err != ESP_OK || v != 0;    /* unreadable: the safe side, delay on */
}

/**
 * @brief Save the "last security access failed" flag in NVS; true if it was saved.
 *        Hook contract: e32p_uds_sec_store_fn.
 */
static bool uds_sec_store(bool failed, void *u)
{
    (void)u;
    const esp_err_t err = e32p_nvs_write_i32(UDS_NVS_NS, UDS_NVS_SEC_FAIL, failed ? 1 : 0);
    if (err != ESP_OK) {
        hil_emit("@EVT UDS sec flag save failed (%s)", esp_err_to_name(err));
        return false;                  /* the server tries again at the next outcome */
    }
    hil_emit("@EVT UDS sec flag=%d", failed ? 1 : 0);
    return true;
}

static esp_err_t uds_peer_init(void)
{
    /* The test secret goes into NVS and is read back, as a product's would be. */
    char buf[UDS_SECRET_LEN + 1u];
    size_t blen = sizeof(buf);
    esp_err_t err = e32p_nvs_write_str(UDS_NVS_NS, UDS_NVS_SECRET, UDS_TEST_SECRET);
    if (err == ESP_OK) {
        err = e32p_nvs_read_str(UDS_NVS_NS, UDS_NVS_SECRET, buf, &blen);
    }
    if (err != ESP_OK) {
        return err;
    }
    if (strlen(buf) != UDS_SECRET_LEN) {
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(s_uds.secret, buf, UDS_SECRET_LEN);

    s_uds.ram = malloc(UDS_SINK_BYTES);
    if (s_uds.ram == NULL) {
        return ESP_ERR_NO_MEM;
    }
    s_uds.setting = UDS_SETTING_BOOT;

    const e32p_uds_config_t sc = {
        .send = uds_send,
        .dids = k_uds_dids, .did_count = sizeof(k_uds_dids) / sizeof(k_uds_dids[0]),
        .on_did_read = uds_did_read, .on_did_write = uds_did_write,
        .on_seed = uds_make_seed, .on_key = uds_check_key,
        .on_sec_load = uds_sec_load, .on_sec_store = uds_sec_store,
        .dtc = (s_peer.obd != NULL) ? &k_uds_dtc : NULL,
        .routines = k_uds_routines, .routine_count = sizeof(k_uds_routines) / sizeof(k_uds_routines[0]),
        .on_routine = uds_routine,
        .sink = &k_uds_sink,
        .on_reset = uds_on_reset,
    };
    err = e32p_uds_init(&sc, &s_uds.h);
    if (err != ESP_OK) {
        return err;
    }

    e32p_isotp_config_t c;
    cfg_for(&c, ECU_UDS, false);                     /* 0x7E1 in, 0x7E9 out */
    c.max_msg_bytes = E32P_UDS_MAX_BLOCK_LEN;        /* one whole 0x36 block */
    c.tx = peer_sink;
    c.on_rx = uds_on_rx;
    c.user = (void *)(intptr_t)E32P_UDS_ADDR_PHYSICAL;
    err = e32p_isotp_init(&c, &s_peer.ch[PEER_UDS_PHYS]);
    if (err != ESP_OK) {
        return err;
    }

    /* Receive-only, on the OBD-II server's broadcast ID. The router offers each
       frame to both servers, and each stays silent on the other's services: an
       unsupported broadcast gets no answer in either standard (D-19). */
    e32p_isotp_config_defaults(&c, RESP_ID(ECU_UDS), OBD_FUNC_11, OBD_FUNC_TA, TESTER_SA, true);
    c.max_msg_bytes = UDS_FUNC_MSG;
    c.tx = peer_sink;
    c.on_rx = uds_on_rx;
    c.user = (void *)(intptr_t)E32P_UDS_ADDR_FUNCTIONAL;
    return e32p_isotp_init(&c, &s_peer.ch[PEER_UDS_FUNC]);
}

static void peer_init(void)
{
    static const struct { uint8_t ecu; uint8_t bs; uint8_t wft; } spec[PEER_ISOTP_N] = {
        [PEER_ECHO]   = { ECU_ECHO,   8, 0 },
        [PEER_FCWAIT] = { ECU_FCWAIT, 4, 1 },   /* WAIT at the first 4-CF boundary */
        [PEER_NCR]    = { ECU_NCR,    8, 0 },
    };
    s_peer.inited = true;
    for (int k = 0; k < PEER_ISOTP_N; k++) {
        e32p_isotp_config_t c;
        cfg_for(&c, spec[k].ecu, false);
        c.rx_block_size = spec[k].bs;
        c.rx_wft_max    = spec[k].wft;
        c.tx    = peer_sink;
        c.on_rx = peer_on_rx;
        c.user  = (void *)(intptr_t)k;
        if (e32p_isotp_init(&c, &s_peer.ch[k]) != ESP_OK) {
            s_peer.failed = true;
            hil_emit("@EVT ISOTP peer init failed ch=%d", k);
            return;
        }
    }
    hil_emit("@EVT ISOTP peer ready" BUILD_FMT, BUILD_ARG);

    /* The servers are independent: one that cannot start leaves its channels NULL
       and the rest keep working. UDS without OBD-II only loses its trouble codes. */
    esp_err_t err = obd_peer_init();
    if (err != ESP_OK) {
        hil_emit("@EVT OBD2 server init failed (%s)", esp_err_to_name(err));
    } else {
        hil_emit("@EVT OBD2 server ready ecu=0x%02X" BUILD_FMT, ECU_OBD, BUILD_ARG);
    }
    err = uds_peer_init();
    if (err != ESP_OK) {
        hil_emit("@EVT UDS server init failed (%s)", esp_err_to_name(err));
        return;
    }
    hil_emit("@EVT UDS server ready ecu=0x%02X" BUILD_FMT, ECU_UDS, BUILD_ARG);
}

void hil_diag_companion_on_frame(const e32p_twai_message_t *m)
{
    if (!s_peer.inited || s_peer.failed) {
        return;
    }
    for (int k = 0; k < PEER_N; k++) {
        if (s_peer.ch[k] != NULL) {
            e32p_isotp_on_frame(s_peer.ch[k], m);   /* each channel filters by its rx id */
        }
    }
}

/** @brief CAN task side of the seed handshake: store P0133 on a clean slate.
 *         The first code stored also freezes the frame. */
static void obd_service_seed(void)
{
    if (__atomic_load_n(&s_peer.seed, __ATOMIC_ACQUIRE) != SEED_REQ) {
        return;
    }
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (s_peer.obd != NULL) {
        err = e32p_obd2_dtc_clear(s_peer.obd);
        if (err == ESP_OK) {
            err = e32p_obd2_dtc_set(s_peer.obd, 0x0133u, true);
        }
    }
    s_peer.seed_err = err;
    __atomic_store_n(&s_peer.seed, SEED_DONE, __ATOMIC_RELEASE);
}

void hil_diag_companion_poll(uint32_t now)
{
    if (!s_peer.inited) {
        peer_init();                             /* first loop pass, at boot */
    }
    if (s_peer.failed) {
        return;
    }
    obd_service_seed();
    uds_service(now);
    for (int k = 0; k < PEER_N; k++) {
        if (s_peer.ch[k] != NULL) {
            e32p_isotp_poll(s_peer.ch[k], now);
        }
    }
}

hil_verdict_t hil_diag_obd2_seed(char *d, size_t n)
{
    if (s_peer.obd == NULL) {
        snprintf(d, n, "no OBD server here (run this on the COMPANION)");
        return HIL_FAIL;
    }
    __atomic_store_n(&s_peer.seed, SEED_REQ, __ATOMIC_RELEASE);
    for (int i = 0; i < 100; i++) {                         /* up to 1 s */
        if (__atomic_load_n(&s_peer.seed, __ATOMIC_ACQUIRE) == SEED_DONE) {
            __atomic_store_n(&s_peer.seed, SEED_IDLE, __ATOMIC_RELEASE);
            if (s_peer.seed_err != ESP_OK) {
                snprintf(d, n, "seed failed (%s)", esp_err_to_name(s_peer.seed_err));
                return HIL_FAIL;
            }
            snprintf(d, n, "seeded P0133 + freeze frame" BUILD_FMT, BUILD_ARG);
            return HIL_PASS;
        }
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    }
    __atomic_store_n(&s_peer.seed, SEED_IDLE, __ATOMIC_RELEASE);
    snprintf(d, n, "CAN task did not pick up the seed request");
    return HIL_FAIL;
}

/* =============================== DUT tests =============================== */

static uint8_t s_tx[XFER_MAX];
static uint8_t s_rx[XFER_MAX];

static esp_err_t bus_up_noack(void)
{
    e32p_twai_deinit();
    e32p_twai_config_t cfg = { .tx_gpio = HIL_TWAI_TX, .rx_gpio = HIL_TWAI_RX,
                               .bitrate_bps = HIL_CAN_BITRATE, .mode = E32P_TWAI_MODE_NO_ACK };
    esp_err_t err = e32p_twai_init(&cfg);
    return (err == ESP_OK) ? e32p_twai_start() : err;
}

/**
 * @brief Teardown in the only safe order (D-03): join the pump, THEN drain and
 *        uninstall TWAI. If the pump does not join, TWAI is deliberately left
 *        installed - deleting the RX queue under a live pump is undefined
 *        behaviour, which is worse than a failed test.
 */
static bool link_down(void)
{
    if (e32p_isotp_twai_stop() != ESP_OK) {
        return false;
    }
    hil_can_bus_down();
    return true;
}

static void fill(uint16_t len, uint8_t seed)
{
    for (uint16_t i = 0; i < len; i++) {
        s_tx[i] = (uint8_t)(i * 7u + seed);
    }
}

/** @brief Send @p len bytes to a companion channel and check the echo. */
static hil_verdict_t echo_xfer(uint8_t ecu, uint16_t len, char *d, size_t n)
{
    if (hil_can_bus_up_normal() != ESP_OK) {
        snprintf(d, n, "bus up failed");
        return HIL_FAIL;
    }
    if (e32p_isotp_twai_start(NULL) != ESP_OK) {
        hil_can_bus_down();
        snprintf(d, n, "link start failed");
        return HIL_FAIL;
    }
    e32p_isotp_config_t c;
    cfg_for(&c, ecu, true);                     /* on_rx NULL -> sync mode */
    e32p_isotp_handle_t ch = NULL;
    uint16_t got = 0;
    esp_err_t err = e32p_isotp_twai_channel(&c, &ch);
    bool match = false;
    if (err == ESP_OK) {
        fill(len, ecu);
        err = e32p_isotp_twai_send_sync(ch, s_tx, len, XFER_MS);
        if (err == ESP_OK) {
            err = e32p_isotp_twai_recv_sync(ch, s_rx, sizeof(s_rx), &got, XFER_MS);
        }
        match = (err == ESP_OK && got == len && memcmp(s_tx, s_rx, len) == 0);
        e32p_isotp_twai_destroy_channel(ch);
    }
    if (!link_down()) {
        snprintf(d, n, "pump did not join");
        return HIL_FAIL;
    }
    if (!match) {
        snprintf(d, n, "len=%u %s got=%u (need companion+xcvr)" BUILD_FMT, len,
                 err != ESP_OK ? esp_err_to_name(err) : "mismatch", got, BUILD_ARG);
        return HIL_FAIL;
    }
    snprintf(d, n, "len=%u" BUILD_FMT, len, BUILD_ARG);
    return HIL_PASS;
}

/* --- single board: two DUT channels looped through self-reception --- */

static e32p_isotp_handle_t s_self_b;

static void self_echo(const uint8_t *data, uint16_t len, void *u)
{
    (void)u;
    (void)e32p_isotp_send(s_self_b, data, len);   /* from the pump: send, not send_sync */
}

/** @brief Needs TWAI up in NO_ACK and the link started. A's frames come back
 *         through the transceiver to B, and B's to A. */
static hil_verdict_t self_loop(uint16_t len, char *d, size_t n)
{
    e32p_isotp_config_t ca, cb;
    cfg_for(&ca, ECU_SELF, true);
    ca.self_reception = true;
    cfg_for(&cb, ECU_SELF, false);
    cb.self_reception = true;
    cb.on_rx = self_echo;                        /* server mode */

    e32p_isotp_handle_t a = NULL;
    if (e32p_isotp_twai_channel(&ca, &a) != ESP_OK ||
        e32p_isotp_twai_channel(&cb, &s_self_b) != ESP_OK) {
        if (a) { e32p_isotp_twai_destroy_channel(a); }
        snprintf(d, n, "channel create failed");
        return HIL_FAIL;
    }
    fill(len, 0x5A);
    uint16_t got = 0;
    esp_err_t err = e32p_isotp_twai_send_sync(a, s_tx, len, XFER_MS);
    if (err == ESP_OK) {
        err = e32p_isotp_twai_recv_sync(a, s_rx, sizeof(s_rx), &got, XFER_MS);
    }
    const bool match = (err == ESP_OK && got == len && memcmp(s_tx, s_rx, len) == 0);
    e32p_isotp_twai_destroy_channel(a);
    e32p_isotp_twai_destroy_channel(s_self_b);
    s_self_b = NULL;
    if (!match) {
        snprintf(d, n, "self len=%u %s got=%u (need xcvr)", len,
                 err != ESP_OK ? esp_err_to_name(err) : "mismatch", got);
        return HIL_FAIL;
    }
    return HIL_PASS;
}

hil_verdict_t hil_diag_isotp_self(char *d, size_t n)
{
    if (bus_up_noack() != ESP_OK) {
        snprintf(d, n, "bus up failed");
        return HIL_FAIL;
    }
    if (e32p_isotp_twai_start(NULL) != ESP_OK) {
        hil_can_bus_down();
        snprintf(d, n, "link start failed");
        return HIL_FAIL;
    }
    hil_verdict_t v = self_loop(100, d, n);
    if (!link_down()) {
        snprintf(d, n, "pump did not join");
        return HIL_FAIL;
    }
    if (v == HIL_PASS) {
        snprintf(d, n, "self len=100" BUILD_FMT, BUILD_ARG);
    }
    return v;
}

/** @brief A deleted task leaves the count, and its stack is freed, only when the
 *         idle task next runs. Allow up to 500 ms before calling it a leak. */
static bool tasks_settle_to(UBaseType_t want)
{
    for (int i = 0; i < 50; i++) {
        if (uxTaskGetNumberOfTasks() == want) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    }
    return uxTaskGetNumberOfTasks() == want;
}

hil_verdict_t hil_diag_isotp_lifecycle(char *d, size_t n)
{
    const UBaseType_t tasks_ref = uxTaskGetNumberOfTasks();   /* before any pump exists */
    size_t heap_ref = 0;

    for (int cyc = 1; cyc <= 3; cyc++) {
        if (bus_up_noack() != ESP_OK) {
            snprintf(d, n, "bus up failed (cycle %d)", cyc);
            return HIL_FAIL;
        }
        if (e32p_isotp_twai_start(NULL) != ESP_OK) {
            hil_can_bus_down();
            snprintf(d, n, "link start failed (cycle %d)", cyc);
            return HIL_FAIL;
        }
        if (e32p_isotp_twai_start(NULL) != ESP_ERR_INVALID_STATE) {   /* D-03 contract */
            link_down();
            snprintf(d, n, "second start not rejected");
            return HIL_FAIL;
        }
        e32p_isotp_config_t c;
        cfg_for(&c, ECU_SELF, true);
        e32p_isotp_handle_t ch = NULL;
        if (e32p_isotp_twai_channel(&c, &ch) != ESP_OK) {
            link_down();
            snprintf(d, n, "attach failed (cycle %d)", cyc);
            return HIL_FAIL;
        }
        e32p_isotp_twai_destroy_channel(ch);
        if (!link_down()) {
            snprintf(d, n, "pump did not join (cycle %d)", cyc);
            return HIL_FAIL;
        }
        if (e32p_isotp_twai_stop() != ESP_OK) {                       /* idempotent */
            snprintf(d, n, "second stop not idempotent");
            return HIL_FAIL;
        }
        if (!tasks_settle_to(tasks_ref)) {
            snprintf(d, n, "tasks %u->%u after cycle %d: a pump leaked",
                     (unsigned)tasks_ref, (unsigned)uxTaskGetNumberOfTasks(), cyc);
            return HIL_FAIL;
        }
        /* Heap reference after the first cycle: the link creates its join
           semaphore and mutex once, on first start, and keeps them. */
        if (cyc == 1) {
            heap_ref = esp_get_free_heap_size();
        }
    }
    const int heap_delta = (int)esp_get_free_heap_size() - (int)heap_ref;
    if (heap_delta < -512) {
        snprintf(d, n, "heap %+dB over 2 cycles: leak", heap_delta);
        return HIL_FAIL;
    }

    /* The real proof: a leaked pump would still be reading the RX queue and
       would steal this transfer's frames. */
    if (bus_up_noack() != ESP_OK || e32p_isotp_twai_start(NULL) != ESP_OK) {
        hil_can_bus_down();
        snprintf(d, n, "final bus/link up failed");
        return HIL_FAIL;
    }
    hil_verdict_t v = self_loop(6, d, n);
    if (!link_down()) {
        snprintf(d, n, "pump did not join (final)");
        return HIL_FAIL;
    }
    if (v == HIL_PASS) {
        snprintf(d, n, "3 cycles tasks=%u heap%+d self len=6" BUILD_FMT,
                 (unsigned)tasks_ref, heap_delta, BUILD_ARG);
    }
    return v;
}

/* --- two boards: against the companion's peer --- */

hil_verdict_t hil_diag_isotp_sf(char *d, size_t n)     { return echo_xfer(ECU_ECHO, 6, d, n); }
hil_verdict_t hil_diag_isotp_mf(char *d, size_t n)     { return echo_xfer(ECU_ECHO, 100, d, n); }
hil_verdict_t hil_diag_isotp_big(char *d, size_t n)    { return echo_xfer(ECU_ECHO, 1024, d, n); }
hil_verdict_t hil_diag_isotp_fcwait(char *d, size_t n) { return echo_xfer(ECU_FCWAIT, 60, d, n); }

hil_verdict_t hil_diag_isotp_ncr(char *d, size_t n)
{
    if (hil_can_bus_up_normal() != ESP_OK) {
        snprintf(d, n, "bus up failed");
        return HIL_FAIL;
    }
    if (e32p_isotp_twai_start(NULL) != ESP_OK) {
        hil_can_bus_down();
        snprintf(d, n, "link start failed");
        return HIL_FAIL;
    }
    e32p_isotp_config_t c;
    cfg_for(&c, ECU_NCR, true);
    e32p_isotp_handle_t ch = NULL;
    const char *why = NULL;
    uint32_t held = 0;

    if (e32p_isotp_twai_channel(&c, &ch) != ESP_OK) {
        why = "channel create failed";
    } else {
        const uint8_t trigger[3] = { 0x3E, 0x00, 0x00 };
        if (e32p_isotp_twai_send_sync(ch, trigger, sizeof(trigger), 1000) != ESP_OK) {
            why = "trigger send failed (need companion+xcvr)";
        } else {
            /* Wait for the companion's First Frame to open a session... */
            uint32_t t_ff = 0;
            for (uint32_t t0 = now_ms(); now_ms() - t0 < 1500; ) {
                if (e32p_isotp_rx_busy(ch)) { t_ff = now_ms(); break; }
                vTaskDelay(pdMS_TO_TICKS(POLL_MS));
            }
            if (t_ff == 0) {
                why = "no First Frame from companion";
            } else {
                /* ...then for N_Cr to discard it. A benign unlocked read of the
                   session state; the pump owns the engine. */
                while (e32p_isotp_rx_busy(ch) && now_ms() - t_ff < 3000) {
                    vTaskDelay(pdMS_TO_TICKS(POLL_MS));
                }
                held = now_ms() - t_ff;
                uint16_t got = 0;
                if (e32p_isotp_rx_busy(ch)) {
                    why = "session never discarded";
                } else if (held < 900 || held > 1300) {
                    why = "discard outside N_Cr window";
                } else if (e32p_isotp_twai_recv_sync(ch, s_rx, sizeof(s_rx), &got, 0) != ESP_ERR_TIMEOUT) {
                    why = "abandoned payload was delivered";
                }
            }
        }
        e32p_isotp_twai_destroy_channel(ch);
    }
    if (!link_down()) {
        snprintf(d, n, "pump did not join");
        return HIL_FAIL;
    }
    if (why != NULL) {
        snprintf(d, n, "%s held=%lums" BUILD_FMT, why, (unsigned long)held, BUILD_ARG);
        return HIL_FAIL;
    }
    snprintf(d, n, "N_Cr discarded after %lums" BUILD_FMT, (unsigned long)held, BUILD_ARG);
    return HIL_PASS;
}

/* ======================= OBD-II: DUT as the tester ======================= */

#define OBD_SILENCE_MS 1000u   /* one N_Bs window: long enough to trust a silence */

static e32p_isotp_handle_t s_obd_phys;   /* 0x7E0 out, 0x7E8 in          */
static e32p_isotp_handle_t s_obd_func;   /* 0x7DF out, transmit-only     */

static bool obd_close(void)
{
    if (s_obd_func != NULL) {
        e32p_isotp_twai_destroy_channel(s_obd_func);
        s_obd_func = NULL;
    }
    if (s_obd_phys != NULL) {
        e32p_isotp_twai_destroy_channel(s_obd_phys);
        s_obd_phys = NULL;
    }
    return link_down();
}

/** @brief Bus up in NORMAL mode and the link started. */
static bool link_up(char *d, size_t n)
{
    if (hil_can_bus_up_normal() != ESP_OK) {
        snprintf(d, n, "bus up failed");
        return false;
    }
    if (e32p_isotp_twai_start(NULL) != ESP_OK) {
        hil_can_bus_down();
        snprintf(d, n, "link start failed");
        return false;
    }
    return true;
}

/** @brief Bus and link up, plus a physical channel and a broadcast channel. */
static bool obd_open(char *d, size_t n)
{
    if (!link_up(d, n)) {
        return false;
    }
    e32p_isotp_config_t c;
    cfg_for(&c, ECU_OBD, true);
    c.max_msg_bytes = OBD_MAX_MSG;
    esp_err_t err = e32p_isotp_twai_channel(&c, &s_obd_phys);
    if (err == ESP_OK) {
        /* Transmit-only: nobody sends to its receive ID. The answer to a
           broadcast arrives from the ECU's own ID, on the physical channel. */
        e32p_isotp_config_defaults(&c, OBD_FUNC_11, OBD_FUNC_11, TESTER_SA, OBD_FUNC_TA, true);
        c.max_msg_bytes = OBD_MAX_MSG;
        err = e32p_isotp_twai_channel(&c, &s_obd_func);
    }
    if (err != ESP_OK) {
        obd_close();
        snprintf(d, n, "channel create failed (%s)", esp_err_to_name(err));
        return false;
    }
    return true;
}

/** @brief Send one request; the answer is read on the physical channel. */
static esp_err_t obd_ask(bool functional, const uint8_t *req, uint16_t req_len,
                         uint16_t *got, uint32_t wait_ms)
{
    *got = 0;
    esp_err_t err = e32p_isotp_twai_send_sync(functional ? s_obd_func : s_obd_phys,
                                              req, req_len, XFER_MS);
    if (err != ESP_OK) {
        return err;
    }
    return e32p_isotp_twai_recv_sync(s_obd_phys, s_rx, sizeof(s_rx), got, wait_ms);
}

/** @brief One exchange whose answer must be exactly @p want. */
static bool obd_expect(bool functional, const uint8_t *req, uint16_t req_len,
                       const uint8_t *want, uint16_t want_len, const char *what,
                       char *d, size_t n)
{
    uint16_t got = 0;
    const esp_err_t err = obd_ask(functional, req, req_len, &got, XFER_MS);
    if (err != ESP_OK) {
        snprintf(d, n, "%s: %s (need companion+xcvr)" BUILD_FMT, what, esp_err_to_name(err), BUILD_ARG);
        return false;
    }
    if (got != want_len || memcmp(s_rx, want, want_len) != 0) {
        snprintf(d, n, "%s: got %u bytes %02X %02X %02X %02X, want %u" BUILD_FMT, what,
                 got, s_rx[0], s_rx[1], s_rx[2], s_rx[3], want_len, BUILD_ARG);
        return false;
    }
    return true;
}

/** @brief Open, run one exact exchange, close. */
static hil_verdict_t obd_single(bool functional, const uint8_t *req, uint16_t req_len,
                                const uint8_t *want, uint16_t want_len, const char *what,
                                char *d, size_t n)
{
    if (!obd_open(d, n)) {
        return HIL_FAIL;
    }
    const bool ok = obd_expect(functional, req, req_len, want, want_len, what, d, n);
    if (!obd_close()) {
        snprintf(d, n, "pump did not join");
        return HIL_FAIL;
    }
    if (ok) {
        snprintf(d, n, "%s ok" BUILD_FMT, what, BUILD_ARG);
    }
    return ok ? HIL_PASS : HIL_FAIL;
}

hil_verdict_t hil_diag_obd2_pid_phys(char *d, size_t n)
{
    static const uint8_t req[] = { 0x01, 0x0C };
    static const uint8_t want[] = { 0x41, 0x0C, 0x1A, 0xF8 };      /* 1726 rpm */
    return obd_single(false, req, sizeof(req), want, sizeof(want), "rpm physical", d, n);
}

hil_verdict_t hil_diag_obd2_pid_func(char *d, size_t n)
{
    static const uint8_t req[] = { 0x01, 0x0D };
    static const uint8_t want[] = { 0x41, 0x0D, 88 };               /* 88 km/h */
    return obd_single(true, req, sizeof(req), want, sizeof(want), "speed broadcast", d, n);
}

hil_verdict_t hil_diag_obd2_vin(char *d, size_t n)
{
    static const uint8_t req[] = { 0x09, 0x02 };
    uint8_t want[3 + E32P_OBD2_VIN_LEN] = { 0x49, 0x02, 0x01 };
    memcpy(&want[3], OBD_VIN, E32P_OBD2_VIN_LEN);                   /* multi-frame */
    return obd_single(false, req, sizeof(req), want, sizeof(want), "vin", d, n);
}

hil_verdict_t hil_diag_obd2_unsupported(char *d, size_t n)
{
    static const uint8_t req[] = { 0x01, 0x42 };
    static const uint8_t want[] = { 0x7F, 0x01, 0x12 };
    if (!obd_open(d, n)) {
        return HIL_FAIL;
    }
    /* The physical half first: it proves the path works, which is what makes the
       silence below mean something rather than a dead ECU. */
    bool ok = obd_expect(false, req, sizeof(req), want, sizeof(want), "unsupported physical", d, n);
    uint16_t got = 0;
    if (ok) {
        const esp_err_t err = obd_ask(true, req, sizeof(req), &got, OBD_SILENCE_MS);
        if (err != ESP_ERR_TIMEOUT) {
            snprintf(d, n, "broadcast drew an answer (%s, %u bytes)" BUILD_FMT,
                     esp_err_to_name(err), got, BUILD_ARG);
            ok = false;
        }
    }
    if (!obd_close()) {
        snprintf(d, n, "pump did not join");
        return HIL_FAIL;
    }
    if (ok) {
        snprintf(d, n, "negative physically, silent on broadcast for %ums" BUILD_FMT,
                 (unsigned)OBD_SILENCE_MS, BUILD_ARG);
    }
    return ok ? HIL_PASS : HIL_FAIL;
}

hil_verdict_t hil_diag_obd2_dtc_cycle(char *d, size_t n)
{
    /* Needs the companion seeded first: host sends RUN obd2_dtc_seed to it. */
    static const uint8_t rd[] = { 0x03 };
    static const uint8_t clr[] = { 0x04 };
    static const uint8_t one[] = { 0x43, 0x01, 0x01, 0x33 };       /* P0133 */
    static const uint8_t cleared[] = { 0x44 };
    static const uint8_t none[] = { 0x43, 0x00 };
    if (!obd_open(d, n)) {
        return HIL_FAIL;
    }
    const bool ok = obd_expect(false, rd, 1, one, sizeof(one), "read seeded code", d, n) &&
                    obd_expect(false, clr, 1, cleared, sizeof(cleared), "clear", d, n) &&
                    obd_expect(false, rd, 1, none, sizeof(none), "read after clear", d, n);
    if (!obd_close()) {
        snprintf(d, n, "pump did not join");
        return HIL_FAIL;
    }
    if (ok) {
        snprintf(d, n, "P0133 read, cleared, gone" BUILD_FMT, BUILD_ARG);
    }
    return ok ? HIL_PASS : HIL_FAIL;
}

hil_verdict_t hil_diag_obd2_freeze(char *d, size_t n)
{
    /* Needs the companion seeded first. The seed froze the frame with 1726 rpm. */
    static const uint8_t rpm[] = { 0x02, 0x0C, 0x00 };
    static const uint8_t rpm_want[] = { 0x42, 0x0C, 0x00, 0x1A, 0xF8 };
    static const uint8_t why[] = { 0x02, 0x02, 0x00 };
    static const uint8_t why_want[] = { 0x42, 0x02, 0x00, 0x01, 0x33 };
    if (!obd_open(d, n)) {
        return HIL_FAIL;
    }
    const bool ok =
        obd_expect(false, rpm, sizeof(rpm), rpm_want, sizeof(rpm_want), "frozen rpm", d, n) &&
        obd_expect(false, why, sizeof(why), why_want, sizeof(why_want), "freezing code", d, n);
    if (!obd_close()) {
        snprintf(d, n, "pump did not join");
        return HIL_FAIL;
    }
    if (ok) {
        snprintf(d, n, "frame 0 holds 1726 rpm, frozen by P0133" BUILD_FMT, BUILD_ARG);
    }
    return ok ? HIL_PASS : HIL_FAIL;
}

/* ======================== UDS: DUT as the tester ======================== */

#define UDS_P2_WAIT_MS   (E32P_UDS_P2_DEFAULT_MS + 50u)   /* P2 plus bus and task slack */
#define UDS_FINAL_MS     (E32P_UDS_P2_STAR_DEFAULT_MS + 1000u)
#define UDS_SILENCE_MS   500u    /* ten times P2: an answer would be here by now   */
#define UDS_KEEP_MS      3000u   /* between keep-alives: well inside S3            */
#define UDS_S3_WAIT_MS   (E32P_UDS_S3_DEFAULT_MS + 300u)
#define UDS_LOCK_WAIT_MS (E32P_UDS_SECURITY_DELAY_MS + 300u)
#define UDS_BLOCK_DATA   (E32P_UDS_MAX_BLOCK_LEN - 2u)    /* 36 <counter> <1024 bytes> */
#define UDS_IMAGE_BYTES  4096u

static e32p_isotp_handle_t s_uds_phys;   /* 0x7E1 out, 0x7E9 in      */
static e32p_isotp_handle_t s_uds_func;   /* 0x7DF out, transmit-only */
static uint8_t s_uds_req[E32P_UDS_MAX_BLOCK_LEN];

static const uint8_t k_seed_req[] = { 0x27, 0x01 };
static const uint8_t k_seed_not_here[] = { 0x7F, 0x27, 0x7F };   /* no security in default */
static const uint8_t k_did_setting[] = { 0x22, 0x01, 0x00 };

static bool uds_close(void)
{
    if (s_uds_func != NULL) {
        e32p_isotp_twai_destroy_channel(s_uds_func);
        s_uds_func = NULL;
    }
    if (s_uds_phys != NULL) {
        e32p_isotp_twai_destroy_channel(s_uds_phys);
        s_uds_phys = NULL;
    }
    return obd_close();                  /* any OBD-II channels, then the link */
}

/**
 * @brief Bus and link up with the UDS channels. @p with_obd adds the OBD-II ones;
 *        they own the broadcast ID on this link, so the UDS broadcast channel then
 *        stays out.
 */
static bool uds_open(bool with_obd, char *d, size_t n)
{
    if (!(with_obd ? obd_open(d, n) : link_up(d, n))) {
        return false;
    }
    e32p_isotp_config_t c;
    cfg_for(&c, ECU_UDS, true);
    c.max_msg_bytes = E32P_UDS_MAX_BLOCK_LEN;
    esp_err_t err = e32p_isotp_twai_channel(&c, &s_uds_phys);
    if (err == ESP_OK && !with_obd) {
        e32p_isotp_config_defaults(&c, OBD_FUNC_11, OBD_FUNC_11, TESTER_SA, OBD_FUNC_TA, true);
        c.max_msg_bytes = UDS_FUNC_MSG;
        err = e32p_isotp_twai_channel(&c, &s_uds_func);
    }
    if (err != ESP_OK) {
        uds_close();
        snprintf(d, n, "channel create failed (%s)", esp_err_to_name(err));
        return false;
    }
    return true;
}

/** @brief The next answer on the physical channel. */
static esp_err_t uds_next(uint16_t *got, uint32_t wait_ms)
{
    *got = 0;
    return e32p_isotp_twai_recv_sync(s_uds_phys, s_rx, sizeof(s_rx), got, wait_ms);
}

static esp_err_t uds_ask(bool functional, const uint8_t *req, uint16_t len, uint16_t *got,
                         uint32_t wait_ms)
{
    *got = 0;
    const esp_err_t err = e32p_isotp_twai_send_sync(functional ? s_uds_func : s_uds_phys,
                                                    req, len, XFER_MS);
    return (err == ESP_OK) ? uds_next(got, wait_ms) : err;
}

/** @brief Judge an answer: @p total bytes long, starting with @p want. */
static bool uds_check(esp_err_t err, uint16_t got, const uint8_t *want, uint16_t want_len,
                      uint16_t total, const char *what, char *d, size_t n)
{
    if (err != ESP_OK) {
        snprintf(d, n, "%s: %s (need companion+xcvr)" BUILD_FMT, what, esp_err_to_name(err), BUILD_ARG);
        return false;
    }
    if (got != total || memcmp(s_rx, want, want_len) != 0) {
        snprintf(d, n, "%s: got %u bytes %02X %02X %02X %02X, want %u" BUILD_FMT, what,
                 got, s_rx[0], s_rx[1], s_rx[2], s_rx[3], total, BUILD_ARG);
        return false;
    }
    return true;
}

static bool uds_expect(bool functional, const uint8_t *req, uint16_t req_len, const uint8_t *want,
                       uint16_t want_len, uint16_t total, const char *what, char *d, size_t n)
{
    uint16_t got = 0;
    const esp_err_t err = uds_ask(functional, req, req_len, &got, XFER_MS);
    return uds_check(err, got, want, want_len, total, what, d, n);
}

/* A physical exchange whose answer must be exactly @p want; sizes from the arrays. */
#define UDS_EXACT(req, want, what) \
    uds_expect(false, (req), sizeof(req), (want), sizeof(want), sizeof(want), (what), d, n)

/** @brief Send, then require that nothing answers for UDS_SILENCE_MS. */
static bool uds_silent(bool functional, const uint8_t *req, uint16_t req_len, const char *what,
                       char *d, size_t n)
{
    uint16_t got = 0;
    const esp_err_t err = uds_ask(functional, req, req_len, &got, UDS_SILENCE_MS);
    if (err != ESP_ERR_TIMEOUT) {
        snprintf(d, n, "%s drew an answer (%s, %u bytes %02X %02X %02X)" BUILD_FMT, what,
                 esp_err_to_name(err), got, s_rx[0], s_rx[1], s_rx[2], BUILD_ARG);
        return false;
    }
    return true;
}

/** @brief 10 <session>: the answer carries P2 = 50 ms and P2* = 5000 ms (in 10 ms). */
static bool uds_session(uint8_t session, char *d, size_t n)
{
    const uint8_t req[] = { 0x10, session };
    const uint8_t want[] = { 0x50, session,
                             (uint8_t)(E32P_UDS_P2_DEFAULT_MS >> 8), (uint8_t)E32P_UDS_P2_DEFAULT_MS,
                             (uint8_t)((E32P_UDS_P2_STAR_DEFAULT_MS / 10u) >> 8),
                             (uint8_t)(E32P_UDS_P2_STAR_DEFAULT_MS / 10u) };
    return UDS_EXACT(req, want, "session");
}

static bool seed_is_zero(const uint8_t *seed)
{
    uint8_t any = 0u;
    for (uint8_t i = 0; i < E32P_UDS_SECURITY_LEN; i++) {
        any |= seed[i];
    }
    return any == 0u;
}

/** @brief 27 01; the seed must be all zero (already unlocked) or not, per @p zero. */
static bool uds_seed(uint8_t *seed, bool zero, const char *what, char *d, size_t n)
{
    static const uint8_t want[] = { 0x67, 0x01 };
    if (!uds_expect(false, k_seed_req, sizeof(k_seed_req), want, sizeof(want),
                    2u + E32P_UDS_SECURITY_LEN, what, d, n)) {
        return false;
    }
    memcpy(seed, &s_rx[2], E32P_UDS_SECURITY_LEN);
    if (seed_is_zero(seed) != zero) {
        snprintf(d, n, "%s: seed %02X%02X%02X%02X, want %s" BUILD_FMT, what, seed[0], seed[1],
                 seed[2], seed[3], zero ? "zero (unlocked)" : "non-zero (locked)", BUILD_ARG);
        return false;
    }
    return true;
}

/** @brief 27 02 with the right key for @p seed, or a wrong one; the answer must be @p want. */
static bool uds_key(const uint8_t *seed, bool right, const uint8_t *want, uint16_t want_len,
                    const char *what, char *d, size_t n)
{
    uint8_t req[2u + E32P_UDS_SECURITY_LEN] = { 0x27, 0x02 };
    if (!uds_key_for((const uint8_t *)UDS_TEST_SECRET, seed, E32P_UDS_SECURITY_LEN, &req[2])) {
        snprintf(d, n, "%s: HMAC failed", what);
        return false;
    }
    if (!right) {
        req[2] ^= 0xFFu;
    }
    return uds_expect(false, req, sizeof(req), want, want_len, want_len, what, d, n);
}

static bool uds_unlock(char *d, size_t n)
{
    static const uint8_t ok[] = { 0x67, 0x02 };
    uint8_t seed[E32P_UDS_SECURITY_LEN];
    return uds_seed(seed, false, "seed", d, n) && uds_key(seed, true, ok, sizeof(ok), "key", d, n);
}

/** @brief Close, and turn the outcome into a verdict; @p ok already wrote @p d on failure. */
static hil_verdict_t uds_finish(bool ok, char *d, size_t n)
{
    if (!uds_close()) {
        snprintf(d, n, "pump did not join");
        return HIL_FAIL;
    }
    return ok ? HIL_PASS : HIL_FAIL;
}

hil_verdict_t hil_diag_uds_session(char *d, size_t n)
{
    static const uint8_t tp[] = { 0x3E, 0x00 };
    static const uint8_t tp_ok[] = { 0x7E, 0x00 };
    static const uint8_t tp_quiet[] = { 0x3E, 0x80 };
    uint8_t seed[E32P_UDS_SECURITY_LEN];
    if (!uds_open(false, d, n)) {
        return HIL_FAIL;
    }
    bool ok = uds_session(0x01, d, n) && UDS_EXACT(tp, tp_ok, "tester present") &&
              UDS_EXACT(k_seed_req, k_seed_not_here, "seed in default") && uds_session(0x03, d, n);
    if (ok) {
        vTaskDelay(pdMS_TO_TICKS(UDS_KEEP_MS));
        ok = uds_silent(false, tp_quiet, sizeof(tp_quiet), "3E 80", d, n);
    }
    if (ok) {
        /* 6 s after 10 03: still extended only because 3E 80 restarted S3. */
        vTaskDelay(pdMS_TO_TICKS(UDS_KEEP_MS - UDS_SILENCE_MS));
        ok = uds_seed(seed, false, "seed 6 s into extended", d, n);
    }
    if (ok) {
        vTaskDelay(pdMS_TO_TICKS(UDS_S3_WAIT_MS));
        ok = UDS_EXACT(k_seed_req, k_seed_not_here, "seed after S3");
    }
    if (ok) {
        snprintf(d, n, "3E 80 kept extended alive; S3 fell back to default" BUILD_FMT, BUILD_ARG);
    }
    return uds_finish(ok, d, n);
}

hil_verdict_t hil_diag_uds_functional(char *d, size_t n)
{
    static const uint8_t tp[] = { 0x3E, 0x00 };
    static const uint8_t tp_ok[] = { 0x7E, 0x00 };
    static const uint8_t tp_quiet[] = { 0x3E, 0x80 };
    static const uint8_t odd[] = { 0x85, 0x01 };          /* ControlDTCSetting: not offered */
    static const uint8_t odd_nrc[] = { 0x7F, 0x85, 0x11 };
    if (!uds_open(false, d, n)) {
        return HIL_FAIL;
    }
    /* The physical refusal runs before its broadcast twin, so that silence means
       suppression, not a dead ECU. */
    const bool ok = uds_expect(true, tp, sizeof(tp), tp_ok, sizeof(tp_ok), sizeof(tp_ok),
                               "3E 00 broadcast", d, n) &&
                    uds_silent(true, tp_quiet, sizeof(tp_quiet), "3E 80 broadcast", d, n) &&
                    UDS_EXACT(odd, odd_nrc, "unsupported physical") &&
                    uds_silent(true, odd, sizeof(odd), "unsupported broadcast", d, n);
    if (ok) {
        snprintf(d, n, "broadcast answered from 0x7E9; 3E 80 and 0x11 silent on broadcast"
                 BUILD_FMT, BUILD_ARG);
    }
    return uds_finish(ok, d, n);
}

hil_verdict_t hil_diag_uds_security(char *d, size_t n)
{
    static const uint8_t bad[] = { 0x7F, 0x27, 0x35 };
    static const uint8_t spent[] = { 0x7F, 0x27, 0x24 };
    static const uint8_t good[] = { 0x67, 0x02 };
    uint8_t s1[E32P_UDS_SECURITY_LEN];
    uint8_t s2[E32P_UDS_SECURITY_LEN];
    if (!uds_open(false, d, n)) {
        return HIL_FAIL;
    }
    const bool ok =
        uds_session(0x03, d, n) &&
        uds_seed(s1, false, "seed 1", d, n) &&
        uds_key(s1, false, bad, sizeof(bad), "wrong key", d, n) &&
        uds_key(s1, true, spent, sizeof(spent), "right key, spent seed", d, n) &&
        uds_seed(s2, false, "seed 2", d, n) &&
        uds_key(s2, true, good, sizeof(good), "right key", d, n) &&
        uds_seed(s2, true, "seed when unlocked", d, n) &&
        uds_session(0x01, d, n) && uds_session(0x03, d, n) &&
        uds_seed(s2, false, "seed after a session change", d, n) &&
        uds_session(0x01, d, n);
    if (ok) {
        snprintf(d, n, "wrong key 0x35, spent seed 0x24, unlocked, zero seed, relocked"
                 BUILD_FMT, BUILD_ARG);
    }
    return uds_finish(ok, d, n);
}

hil_verdict_t hil_diag_uds_did_write(char *d, size_t n)
{
    static const uint8_t vin_req[] = { 0x22, 0xF1, 0x90 };
    static const uint8_t no_session[] = { 0x7F, 0x2E, 0x31 };
    static const uint8_t no_security[] = { 0x7F, 0x2E, 0x33 };
    static const uint8_t wr_ok[] = { 0x6E, 0x01, 0x00 };
    uint8_t vin_want[3u + E32P_OBD2_VIN_LEN] = { 0x62, 0xF1, 0x90 };
    memcpy(&vin_want[3], OBD_VIN, E32P_OBD2_VIN_LEN);   /* 20 bytes: a multi-frame answer */
    const uint32_t v = esp_random() | 0x80000000u;      /* never the boot value */
    uint8_t wr[7] = { 0x2E, 0x01, 0x00 };
    uint8_t rd_want[7] = { 0x62, 0x01, 0x00 };
    put32(&wr[3], v);
    put32(&rd_want[3], v);
    if (!uds_open(false, d, n)) {
        return HIL_FAIL;
    }
    const bool ok = uds_session(0x01, d, n) &&
                    UDS_EXACT(vin_req, vin_want, "VIN") &&
                    UDS_EXACT(wr, no_session, "write in default") &&
                    uds_session(0x03, d, n) &&
                    UDS_EXACT(wr, no_security, "write while locked") &&
                    uds_unlock(d, n) &&
                    UDS_EXACT(wr, wr_ok, "write") &&
                    UDS_EXACT(k_did_setting, rd_want, "read back") &&
                    uds_session(0x01, d, n);
    if (ok) {
        snprintf(d, n, "VIN read; write needs extended (0x31) and security (0x33); %08lX read back"
                 BUILD_FMT, (unsigned long)v, BUILD_ARG);
    }
    return uds_finish(ok, d, n);
}

hil_verdict_t hil_diag_uds_dtc(char *d, size_t n)
{
    static const uint8_t obd_clear[] = { 0x04 };
    static const uint8_t obd_cleared[] = { 0x44 };
    static const uint8_t obd_read[] = { 0x03 };
    static const uint8_t obd_one[] = { 0x43, 0x01, 0x02, 0x17 };           /* P0217 */
    static const uint8_t obd_none[] = { 0x43, 0x00 };
    static const uint8_t list[] = { 0x19, 0x02, 0xFF };
    static const uint8_t list_none[] = { 0x59, 0x02, 0x0C };
    static const uint8_t list_one[] = { 0x59, 0x02, 0x0C, 0x02, 0x17, 0x00, 0x08 };
    static const uint8_t count[] = { 0x19, 0x01, 0xFF };
    static const uint8_t count_one[] = { 0x59, 0x01, 0x0C, 0x00, 0x00, 0x01 };   /* ISO 15031 */
    static const uint8_t inject[] = { 0x31, 0x01, 0x02, 0x01 };
    static const uint8_t injected[] = { 0x71, 0x01, 0x02, 0x01, 0x00 };
    static const uint8_t clear[] = { 0x14, 0xFF, 0xFF, 0xFF };
    static const uint8_t cleared[] = { 0x54 };
    if (!uds_open(true, d, n)) {
        return HIL_FAIL;
    }
    const bool ok =
        obd_expect(false, obd_clear, sizeof(obd_clear), obd_cleared, sizeof(obd_cleared),
                   "OBD-II clear", d, n) &&
        UDS_EXACT(list, list_none, "UDS list, empty") &&
        uds_session(0x03, d, n) &&
        UDS_EXACT(inject, injected, "inject P0217") &&
        UDS_EXACT(count, count_one, "UDS count") &&
        UDS_EXACT(list, list_one, "UDS list") &&
        obd_expect(false, obd_read, sizeof(obd_read), obd_one, sizeof(obd_one),
                   "OBD-II sees it", d, n) &&
        UDS_EXACT(clear, cleared, "UDS clear") &&
        obd_expect(false, obd_read, sizeof(obd_read), obd_none, sizeof(obd_none),
                   "OBD-II sees it gone", d, n) &&
        uds_session(0x01, d, n);
    if (ok) {
        snprintf(d, n, "P0217 set by routine, listed by 19, seen by OBD 03, cleared by 14"
                 BUILD_FMT, BUILD_ARG);
    }
    return uds_finish(ok, d, n);
}

static const uint8_t k_erase[] = { 0x31, 0x01, 0xFF, 0x00 };
static const uint8_t k_erase_wait[] = { 0x7F, 0x31, 0x78 };
static const uint8_t k_erase_done[] = { 0x71, 0x01, 0xFF, 0x00, 0x00 };

hil_verdict_t hil_diag_uds_erase(char *d, size_t n)
{
    static const uint8_t tp[] = { 0x3E, 0x00 };
    static const uint8_t busy[] = { 0x7F, 0x3E, 0x21 };
    uint32_t t_wait = 0;
    uint32_t t_done = 0;
    if (!uds_open(false, d, n)) {
        return HIL_FAIL;
    }
    bool ok = uds_session(0x02, d, n) && uds_unlock(d, n);
    if (ok) {
        uint16_t got = 0;
        const uint32_t t0 = now_ms();
        esp_err_t err = uds_ask(false, k_erase, sizeof(k_erase), &got, UDS_P2_WAIT_MS);
        t_wait = now_ms() - t0;
        ok = uds_check(err, got, k_erase_wait, sizeof(k_erase_wait), sizeof(k_erase_wait),
                       "0x78 within P2", d, n) &&
             UDS_EXACT(tp, busy, "request while pending");
        if (ok) {
            err = uds_next(&got, UDS_FINAL_MS);
            t_done = now_ms() - t0;
            ok = uds_check(err, got, k_erase_done, sizeof(k_erase_done), sizeof(k_erase_done),
                           "erase result", d, n);
        }
        if (ok && (t_done + 100u < UDS_ERASE_MS || t_done > UDS_ERASE_MS + 500u)) {
            snprintf(d, n, "erase answered after %lums, want about %ums" BUILD_FMT,
                     (unsigned long)t_done, (unsigned)UDS_ERASE_MS, BUILD_ARG);
            ok = false;
        }
    }
    ok = ok && uds_session(0x01, d, n);
    if (ok) {
        snprintf(d, n, "0x78 after %lums, 0x21 while pending, done after %lums" BUILD_FMT,
                 (unsigned long)t_wait, (unsigned long)t_done, BUILD_ARG);
    }
    return uds_finish(ok, d, n);
}

/** @brief 31 01 FF00 and wait through the 0x78 for its result. */
static bool uds_erase_and_wait(char *d, size_t n)
{
    uint16_t got = 0;
    esp_err_t err = uds_ask(false, k_erase, sizeof(k_erase), &got, XFER_MS);
    if (!uds_check(err, got, k_erase_wait, sizeof(k_erase_wait), sizeof(k_erase_wait),
                   "erase", d, n)) {
        return false;
    }
    err = uds_next(&got, UDS_FINAL_MS);
    return uds_check(err, got, k_erase_done, sizeof(k_erase_done), sizeof(k_erase_done),
                     "erase result", d, n);
}

static uint8_t image_byte(uint32_t i)
{
    return (uint8_t)(i * 7u + 3u);       /* the pattern uds_client.py downloads */
}

hil_verdict_t hil_diag_uds_download(char *d, size_t n)
{
    static const uint8_t request[] = { 0x34, 0x00, 0x44, 0x00, 0x00, 0x00, 0x00,
                                       (uint8_t)(UDS_IMAGE_BYTES >> 24), (uint8_t)(UDS_IMAGE_BYTES >> 16),
                                       (uint8_t)(UDS_IMAGE_BYTES >> 8), (uint8_t)UDS_IMAGE_BYTES };
    static const uint8_t accepted[] = { 0x74, 0x20, (uint8_t)(E32P_UDS_MAX_BLOCK_LEN >> 8),
                                        (uint8_t)E32P_UDS_MAX_BLOCK_LEN };
    static const uint8_t exit_req[] = { 0x37 };
    static const uint8_t exit_ok[] = { 0x77 };
    _Static_assert(UDS_IMAGE_BYTES % UDS_BLOCK_DATA == 0u, "whole blocks only");
    uint32_t crc = 0xFFFFFFFFu;
    uint32_t t_xfer = 0;
    if (!uds_open(false, d, n)) {
        return HIL_FAIL;
    }
    bool ok = uds_session(0x02, d, n) && uds_unlock(d, n) && uds_erase_and_wait(d, n) &&
              UDS_EXACT(request, accepted, "request download");
    const uint32_t t0 = now_ms();
    for (uint32_t blk = 1u; ok && blk <= UDS_IMAGE_BYTES / UDS_BLOCK_DATA; blk++) {
        s_uds_req[0] = 0x36;
        s_uds_req[1] = (uint8_t)blk;
        for (uint32_t i = 0; i < UDS_BLOCK_DATA; i++) {
            s_uds_req[2u + i] = image_byte((blk - 1u) * UDS_BLOCK_DATA + i);
        }
        crc = crc32_step(crc, &s_uds_req[2], UDS_BLOCK_DATA);
        const uint8_t want[] = { 0x76, (uint8_t)blk };
        ok = uds_expect(false, s_uds_req, E32P_UDS_MAX_BLOCK_LEN, want, sizeof(want), sizeof(want),
                        "transfer data", d, n);
    }
    t_xfer = now_ms() - t0;
    crc = ~crc;
    ok = ok && UDS_EXACT(exit_req, exit_ok, "transfer exit");

    /* FF01 against the right CRC, then a wrong one: 00 match / 01 mismatch, then the ECU's. */
    for (uint8_t wrong = 0u; ok && wrong <= 1u; wrong++) {
        uint8_t check[8] = { 0x31, 0x01, 0xFF, 0x01 };
        uint8_t want[9] = { 0x71, 0x01, 0xFF, 0x01, wrong };
        put32(&check[4], crc ^ wrong);
        put32(&want[5], crc);
        ok = UDS_EXACT(check, want, wrong ? "check, wrong CRC" : "check");
    }
    ok = ok && uds_session(0x01, d, n);
    if (ok) {
        snprintf(d, n, "%u bytes in %u-byte blocks in %lums, CRC-32 %08lX matched, a wrong one caught"
                 BUILD_FMT, (unsigned)UDS_IMAGE_BYTES, (unsigned)UDS_BLOCK_DATA,
                 (unsigned long)t_xfer, (unsigned long)crc, BUILD_ARG);
    }
    return uds_finish(ok, d, n);
}

hil_verdict_t hil_diag_uds_lockout(char *d, size_t n)
{
    static const uint8_t bad[] = { 0x7F, 0x27, 0x35 };
    static const uint8_t over[] = { 0x7F, 0x27, 0x36 };
    static const uint8_t wait[] = { 0x7F, 0x27, 0x37 };
    uint8_t seed[E32P_UDS_SECURITY_LEN];
    if (!uds_open(false, d, n)) {
        return HIL_FAIL;
    }
    bool ok = uds_session(0x03, d, n);
    for (uint8_t i = 1u; ok && i <= E32P_UDS_SECURITY_MAX_ATTEMPTS; i++) {
        const bool last = (i == E32P_UDS_SECURITY_MAX_ATTEMPTS);
        ok = uds_seed(seed, false, "seed", d, n) &&
             uds_key(seed, false, last ? over : bad, sizeof(bad), last ? "last wrong key" : "wrong key", d, n);
    }
    const uint32_t t_lock = now_ms();
    ok = ok && UDS_EXACT(k_seed_req, wait, "seed while locked out") &&
         uds_session(0x01, d, n) && uds_session(0x03, d, n) &&
         UDS_EXACT(k_seed_req, wait, "seed after a session change");
    if (ok) {
        while (now_ms() - t_lock < UDS_LOCK_WAIT_MS) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        /* S3 has long run out: back into extended, and the delay must be over. */
        ok = uds_session(0x03, d, n) && uds_unlock(d, n) && uds_session(0x01, d, n);
    }
    if (ok) {
        snprintf(d, n, "third wrong key 0x36, 0x37 across a session change, unlock after %us"
                 BUILD_FMT, (unsigned)(UDS_LOCK_WAIT_MS / 1000u), BUILD_ARG);
    }
    return uds_finish(ok, d, n);
}

hil_verdict_t hil_diag_uds_reset(char *d, size_t n)
{
    static const uint8_t reset[] = { 0x11, 0x01 };
    static const uint8_t reset_ok[] = { 0x51, 0x01 };
    static const uint8_t wr_ok[] = { 0x6E, 0x01, 0x00 };
    const uint32_t v = esp_random() | 0x80000000u;      /* never the boot value */
    uint8_t wr[7] = { 0x2E, 0x01, 0x00 };
    uint8_t rd_want[7] = { 0x62, 0x01, 0x00 };
    put32(&wr[3], v);
    put32(&rd_want[3], v);
    if (!uds_open(false, d, n)) {
        return HIL_FAIL;
    }
    /* Leave a mark only a reboot erases, then reset. The host watches the companion
       restart and then runs uds_after_reset. */
    const bool ok = uds_session(0x03, d, n) && uds_unlock(d, n) &&
                    UDS_EXACT(wr, wr_ok, "write") &&
                    UDS_EXACT(k_did_setting, rd_want, "read back") &&
                    UDS_EXACT(reset, reset_ok, "reset");
    if (ok) {
        snprintf(d, n, "51 01 after setting %08lX" BUILD_FMT, (unsigned long)v, BUILD_ARG);
    }
    return uds_finish(ok, d, n);
}

hil_verdict_t hil_diag_uds_sec_reset(char *d, size_t n)
{
    static const uint8_t bad[] = { 0x7F, 0x27, 0x35 };
    static const uint8_t over[] = { 0x7F, 0x27, 0x36 };
    static const uint8_t reset[] = { 0x11, 0x01 };
    static const uint8_t reset_ok[] = { 0x51, 0x01 };
    uint8_t seed[E32P_UDS_SECURITY_LEN];
    if (!uds_open(false, d, n)) {
        return HIL_FAIL;
    }
    /* First put the companion in a known state: unlock (a right key stores 0 if a
       stale 1 was there) and relock with two session changes. Then the first wrong key
       always changes the flag to 1, so the host can count on '@EVT UDS sec flag=1'.
       Three wrong keys, then a reset with the lockout still running; the host waits
       for the restart, then runs the test 'uds_sec_after' at once. The round trip of
       the first wrong key is the request that saves the flag (one NVS write before the
       answer); it is measured and reported against P2 = 50 ms. */
    int64_t first_us = 0;
    bool ok = uds_session(0x03, d, n) && uds_unlock(d, n) &&
              uds_session(0x01, d, n) && uds_session(0x03, d, n);
    for (uint8_t i = 1u; ok && i <= E32P_UDS_SECURITY_MAX_ATTEMPTS; i++) {
        const bool last = (i == E32P_UDS_SECURITY_MAX_ATTEMPTS);
        ok = uds_seed(seed, false, "seed", d, n);
        const int64_t t0 = esp_timer_get_time();
        ok = ok && uds_key(seed, false, last ? over : bad, sizeof(bad),
                           last ? "last wrong key" : "wrong key", d, n);
        if (i == 1u) {
            first_us = esp_timer_get_time() - t0;
        }
    }
    ok = ok && UDS_EXACT(reset, reset_ok, "reset");
    if (ok) {
        snprintf(d, n, "3 wrong keys (0x36), then 51 01; first wrong key answered in %u.%u ms" BUILD_FMT,
                 (unsigned)(first_us / 1000), (unsigned)((first_us % 1000) / 100), BUILD_ARG);
    }
    return uds_finish(ok, d, n);
}

hil_verdict_t hil_diag_uds_sec_after_reset(char *d, size_t n)
{
    static const uint8_t wait[] = { 0x7F, 0x27, 0x37 };
    if (!uds_open(false, d, n)) {
        return HIL_FAIL;
    }
    /* Right after the restart the delay must still run (0x37). Then wait it out and
       unlock: the right key clears the saved flag, so later boots start clean. */
    bool ok = uds_session(0x03, d, n) && UDS_EXACT(k_seed_req, wait, "seed right after the reset");
    if (ok) {
        const uint32_t t0 = now_ms();
        while (now_ms() - t0 < UDS_LOCK_WAIT_MS) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        /* S3 has run out: back into extended, and the delay must be over. */
        ok = uds_session(0x03, d, n) && uds_unlock(d, n) && uds_session(0x01, d, n);
    }
    if (ok) {
        snprintf(d, n, "0x37 after the reset, unlock after %us" BUILD_FMT,
                 (unsigned)(UDS_LOCK_WAIT_MS / 1000u), BUILD_ARG);
    }
    return uds_finish(ok, d, n);
}

hil_verdict_t hil_diag_uds_after_reset(char *d, size_t n)
{
    static const uint8_t at_boot[] = { 0x62, 0x01, 0x00,
                                       (uint8_t)(UDS_SETTING_BOOT >> 24), (uint8_t)(UDS_SETTING_BOOT >> 16),
                                       (uint8_t)(UDS_SETTING_BOOT >> 8), (uint8_t)UDS_SETTING_BOOT };
    uint8_t seed[E32P_UDS_SECURITY_LEN];
    if (!uds_open(false, d, n)) {
        return HIL_FAIL;
    }
    const bool ok = UDS_EXACT(k_did_setting, at_boot, "setting back at its boot value") &&
                    UDS_EXACT(k_seed_req, k_seed_not_here, "default session") &&
                    uds_session(0x03, d, n) &&
                    uds_seed(seed, false, "locked", d, n) &&
                    uds_session(0x01, d, n);
    if (ok) {
        snprintf(d, n, "rebooted: setting at boot value, default session, locked" BUILD_FMT, BUILD_ARG);
    }
    return uds_finish(ok, d, n);
}
