/**
 * @file main.c
 * @brief 33_uds_server — a UDS ECU on the CAN bus, with a reflash path.
 *
 * What a tester finds on 0x7E1 -> 0x7E9 (ECU 0x11, 11-bit):
 *   - sessions: default, programming, extended (S3 = 5 s; 3E 80 keeps one alive)
 *   - DIDs: F190 VIN, F18C serial (from the MAC), F195 software version — read
 *     anywhere; 0100 a 4-byte setting, written in the extended session with
 *     security, kept in NVS
 *   - security access level 1: key = HMAC-SHA256(secret, seed), first 4 bytes. The
 *     secret is provisioned over the USB serial console (uds_client.py provision)
 *     and never leaves the device again
 *   - trouble codes (19 / 14), and routine 0201, which stores P0217 to read back
 *   - the reflash: 31 01 FF00 erase, 34 / 36 / 37, 31 01 FF01 check, 11 01 reset —
 *     into RAM (default, safe) or into the other OTA slot (Kconfig)
 *
 * Every UDS callback runs in the glue's task (see e32p_uds_isotp.h).
 */
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/twai.h"   /* raw status and recovery for bus-off */
#include "driver/uart.h"
#include "bootloader_random.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_system.h"
#include "mbedtls/md.h"
#include "e32p_nvs.h"
#include "e32p_ota.h"
#include "e32p_twai.h"
#include "e32p_isotp.h"
#include "e32p_isotp_twai.h"
#include "e32p_uds.h"
#include "e32p_uds_isotp.h"
#if CONFIG_EX33_SINK_OTA
#include "e32p_uds_ota.h"
#endif

static const char *TAG = "ex33";

#define NVS_NS        "ex33"
#define NVS_SECRET    "secret"       /* 32 bytes, stored as 64 hex characters */
#define NVS_SETTING   "setting"      /* DID 0x0100                            */
#define NVS_SEC_FAIL  "sec_fail"     /* 1 = the last security access failed   */
#define SECRET_LEN    32u

#define DID_VIN       0xF190u
#define DID_SERIAL    0xF18Cu
#define DID_SW        0xF195u
#define DID_SETTING   0x0100u

#define RID_INJECT    0x0201u        /* stores P0217, so there is a code to read */
#define RID_ERASE     0xFF00u
#define RID_CHECK     0xFF01u

#define UDS_RC_REQUEST_RESULTS 0x03u /* 31 sub-function: requestRoutineResults */

#define PROVISION_CMD     "provision "
#define PROVISION_CMD_LEN (sizeof(PROVISION_CMD) - 1u)

/* s_secret is written once, then s_have_secret is set with a release store. Readers on
   the uds task load s_have_secret with acquire and touch s_secret only when it is true.
   Provisioning is one-time (the console refuses when a secret exists), so the secret is
   never rewritten while a reader may be using it. */
static uint8_t     s_secret[SECRET_LEN];
static atomic_bool s_have_secret;
static uint32_t s_setting = 0x00000001u;
static char     s_sw_version[32];   /* from the app descriptor, read once at boot */

/* ---- the security secret ------------------------------------------------------ */

static int hexval(char c)
{
    if (c >= '0' && c <= '9') { return c - '0'; }
    if (c >= 'a' && c <= 'f') { return c - 'a' + 10; }
    if (c >= 'A' && c <= 'F') { return c - 'A' + 10; }
    return -1;
}

/** @brief Parse 64 hex characters into @p out; false on anything else. */
static bool parse_secret(const char *hex, uint8_t out[SECRET_LEN])
{
    if (strlen(hex) != 2u * SECRET_LEN) {
        return false;
    }
    for (size_t i = 0; i < SECRET_LEN; i++) {
        const int hi = hexval(hex[2 * i]);
        const int lo = hexval(hex[2 * i + 1]);
        if (hi < 0 || lo < 0) {
            return false;
        }
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return true;
}

static void load_secret(void)
{
    char hex[2 * SECRET_LEN + 1];
    size_t len = sizeof(hex);
    const esp_err_t err = e32p_nvs_read_str(NVS_NS, NVS_SECRET, hex, &len);
    bool ok = false;
    if (err == ESP_OK) {
        ok = parse_secret(hex, s_secret);
        if (!ok) {
            ESP_LOGW(TAG, "stored secret is malformed: not used");
            memset(s_secret, 0, sizeof(s_secret));   /* no partial secret left in RAM */
        }
    } else if (!e32p_nvs_is_not_found(err)) {
        ESP_LOGW(TAG, "secret not read: %s", esp_err_to_name(err));
    }
    atomic_store_explicit(&s_have_secret, ok, memory_order_release);
    memset(hex, 0, sizeof(hex));
}

static bool have_secret(void)
{
    return atomic_load_explicit(&s_have_secret, memory_order_acquire);
}

#if CONFIG_EX33_CONSOLE_PROVISIONING
/** @brief True only if the secret key is certainly missing; any other result counts as stored. */
static bool secret_is_missing(void)
{
    size_t len = 0;   /* length query only: the secret is not copied out */
    const esp_err_t err = e32p_nvs_read_str(NVS_NS, NVS_SECRET, NULL, &len);
    if (err != ESP_OK && !e32p_nvs_is_not_found(err)) {
        ESP_LOGW(TAG, "secret store not readable: %s", esp_err_to_name(err));
    }
    return e32p_nvs_is_not_found(err);
}

/**
 * @brief Read "provision <64 hex chars>" lines from the USB console and store the
 *        secret. Never echoes it. One-time: when a secret is already stored (or the
 *        store cannot be read) it answers @PROVISION REFUSED and writes nothing.
 *        Physical USB access is the trust boundary — a production build must not
 *        include this (security audit, item 1).
 */
static void console_task(void *arg)
{
    (void)arg;
    char line[96];
    size_t n = 0;
    for (;;) {
        uint8_t ch;
        if (uart_read_bytes(UART_NUM_0, &ch, 1, portMAX_DELAY) != 1) {
            continue;
        }
        if (ch != '\n' && ch != '\r') {
            if (n < sizeof(line) - 1u) {
                line[n++] = (char)ch;
            } else {
                n = 0;   /* overlong: drop it */
            }
            continue;
        }
        line[n] = '\0';
        n = 0;
        if (strncmp(line, PROVISION_CMD, PROVISION_CMD_LEN) != 0) {
            continue;
        }
        uint8_t secret[SECRET_LEN];
        esp_err_t err = ESP_OK;
        if (!secret_is_missing()) {
            printf("@PROVISION REFUSED\n");
        } else if (!parse_secret(&line[PROVISION_CMD_LEN], secret)) {
            printf("@PROVISION FAILED\n");
        } else if ((err = e32p_nvs_write_str(NVS_NS, NVS_SECRET, &line[PROVISION_CMD_LEN])) !=
                   ESP_OK) {
            ESP_LOGW(TAG, "secret not saved: %s", esp_err_to_name(err));
            printf("@PROVISION FAILED\n");
        } else {
            memcpy(s_secret, secret, SECRET_LEN);
            atomic_store_explicit(&s_have_secret, true, memory_order_release);
            printf("@PROVISIONED\n");
        }
        memset(secret, 0, sizeof(secret));
        memset(line, 0, sizeof(line));
    }
}
#endif

/* ---- data identifiers --------------------------------------------------------- */

static const e32p_uds_did_t k_dids[] = {
    { DID_VIN,     17u, E32P_UDS_IN_ANY, 0u,                   false },
    { DID_SERIAL,   8u, E32P_UDS_IN_ANY, 0u,                   false },
    { DID_SW,       8u, E32P_UDS_IN_ANY, 0u,                   false },
    { DID_SETTING,  4u, E32P_UDS_IN_ANY, E32P_UDS_IN_EXTENDED, true  },
};

static e32p_uds_nrc_t did_read(uint16_t did, uint8_t *out, uint16_t len, void *user)
{
    (void)user;
    switch (did) {
    case DID_VIN:
        memcpy(out, "E32PUDS0000000033", len);
        return E32P_UDS_NRC_OK;
    case DID_SERIAL: {
        uint8_t mac[6];
        (void)esp_read_mac(mac, ESP_MAC_WIFI_STA);
        char sn[9];
        snprintf(sn, sizeof(sn), "%02X%02X%02X%02X", mac[2], mac[3], mac[4], mac[5]);
        memcpy(out, sn, len);
        return E32P_UDS_NRC_OK;
    }
    case DID_SW: {
        /* The version the running image was built with (CONFIG_APP_PROJECT_VER),
           space-padded: after a reflash it names the new image. */
        memset(out, ' ', len);
        memcpy(out, s_sw_version, strnlen(s_sw_version, len));
        return E32P_UDS_NRC_OK;
    }
    case DID_SETTING:
        out[0] = (uint8_t)(s_setting >> 24);
        out[1] = (uint8_t)(s_setting >> 16);
        out[2] = (uint8_t)(s_setting >> 8);
        out[3] = (uint8_t)s_setting;
        return E32P_UDS_NRC_OK;
    default:
        return E32P_UDS_NRC_OUT_OF_RANGE;
    }
}

static e32p_uds_nrc_t did_write(uint16_t did, const uint8_t *data, uint16_t len, void *user)
{
    (void)user;
    (void)len;
    if (did != DID_SETTING) {
        return E32P_UDS_NRC_OUT_OF_RANGE;
    }
    const uint32_t v = ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
                       ((uint32_t)data[2] << 8) | data[3];
    if (e32p_nvs_write_i32(NVS_NS, NVS_SETTING, (int32_t)v) != ESP_OK) {
        return E32P_UDS_NRC_PROGRAMMING_FAILURE;
    }
    s_setting = v;
    return E32P_UDS_NRC_OK;
}

/* ---- security access ---------------------------------------------------------- */

static e32p_uds_nrc_t make_seed(uint8_t level, uint8_t *seed, uint8_t len, void *user)
{
    (void)level;
    (void)user;
    if (!have_secret()) {
        ESP_LOGW(TAG, "seed refused: no secret provisioned (uds_client.py provision)");
        return E32P_UDS_NRC_CONDITIONS_NOT_CORRECT;
    }
    bool zero = true;
    while (zero) {   /* an all-zero seed would mean "already unlocked" */
        esp_fill_random(seed, len);
        for (uint8_t i = 0; i < len; i++) {
            zero = zero && seed[i] == 0u;
        }
    }
    return E32P_UDS_NRC_OK;
}

static bool check_key(uint8_t level, const uint8_t *seed, const uint8_t *key, uint8_t len,
                      void *user)
{
    (void)user;
    if (level != 1u || !have_secret()) {
        return false;
    }
    uint8_t mac[32];
    if (mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), s_secret, SECRET_LEN,
                        seed, len, mac) != 0) {
        return false;
    }
    uint8_t diff = 0u;   /* constant time: no early exit on the first wrong byte */
    for (uint8_t i = 0; i < len; i++) {
        diff |= (uint8_t)(mac[i] ^ key[i]);
    }
    memset(mac, 0, sizeof(mac));
    return diff == 0u;
}

/**
 * @brief The saved "the last security access failed" flag (NVS ns "ex33", key "sec_fail").
 *        Hook contract: e32p_uds_sec_load_fn.
 */
static bool sec_load(void *user)
{
    (void)user;
    int32_t v = 0;
    const esp_err_t err = e32p_nvs_read_i32(NVS_NS, NVS_SEC_FAIL, &v);
    if (err != ESP_OK && !e32p_nvs_is_not_found(err)) {
        ESP_LOGW(TAG, "security flag not read: %s; start delay on", esp_err_to_name(err));
    }
    if (e32p_nvs_is_not_found(err)) {
        /* Missing is answered false (header contract): this image has recorded no failure.
           Limit: a board upgraded in place from an older image that stored a secret but
           never wrote this flag also reads "missing", so a wrong key made just before the
           upgrade is not carried over. An NVS wipe removes the secret too (no seed until
           a re-provision). */
        return false;
    }
    return err != ESP_OK || v != 0;   /* unreadable: the safe side, delay on */
}

/** @brief Save the flag; the real NVS result, so the server retries at the next outcome. */
static bool sec_store(bool failed, void *user)
{
    (void)user;
    const esp_err_t err = e32p_nvs_write_i32(NVS_NS, NVS_SEC_FAIL, failed ? 1 : 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "security flag not saved: %s", esp_err_to_name(err));
    }
    return err == ESP_OK;
}

/* ---- trouble codes ------------------------------------------------------------ */

#define MAX_DTC 8u

static struct {
    uint32_t dtc;
    uint8_t  status;
} s_dtc[MAX_DTC];
static uint16_t s_dtc_count;

static uint16_t dtc_count(void *user)
{
    (void)user;
    return s_dtc_count;
}

static bool dtc_at(uint16_t index, uint32_t *dtc, uint8_t *status, void *user)
{
    (void)user;
    if (index >= s_dtc_count) {
        return false;
    }
    *dtc = s_dtc[index].dtc;
    *status = s_dtc[index].status;
    return true;
}

static e32p_uds_nrc_t dtc_clear(uint32_t group, void *user)
{
    (void)group;
    (void)user;
    s_dtc_count = 0u;
    return E32P_UDS_NRC_OK;
}

static const e32p_uds_dtc_provider_t k_dtc = {
    0x0Du, E32P_UDS_DTC_FORMAT_ISO15031, dtc_count, dtc_at, dtc_clear,   /* testFailed, pending, confirmed */
};

/* ---- the download sink: RAM, or the other OTA slot ---------------------------- */

#if CONFIG_EX33_SINK_RAM
static uint8_t      *s_ram;
static uint32_t      s_ram_len;
static uint32_t      s_ram_crc;
static volatile bool s_erasing;
static volatile bool s_erased;
static bool          s_erase_started;   /* a start of FF00 since boot: 31 03 may ask for results */
static bool          s_check_started;   /* a start of FF01 since boot or the last 0x34 */
static bool          s_check_match;     /* the last FF01 result */

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

static void ram_erase_worker(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(CONFIG_EX33_ERASE_MS));   /* long enough to need 0x78 */
    memset(s_ram, 0xFF, CONFIG_EX33_RAM_SINK_SIZE);
    s_erased = true;
    s_erasing = false;
    const uint8_t status = 0x00u;
    (void)e32p_uds_isotp_complete(E32P_UDS_NRC_OK, &status, 1u);
    vTaskDelete(NULL);
}

static e32p_uds_nrc_t ram_begin(uint32_t size, void *user)
{
    (void)user;
    if (s_erasing || !s_erased) {
        return E32P_UDS_NRC_CONDITIONS_NOT_CORRECT;   /* erase first: 31 01 FF00 */
    }
    if (size > CONFIG_EX33_RAM_SINK_SIZE) {
        return E32P_UDS_NRC_DOWNLOAD_NOT_ACCEPTED;
    }
    s_ram_len = 0u;
    s_ram_crc = 0u;            /* no complete image until 37: FF01 must not check the last one */
    s_check_started = false;   /* a new download: 31 03 FF01 answers 0x24 until the next check */
    return E32P_UDS_NRC_OK;
}

static e32p_uds_nrc_t ram_write(const uint8_t *data, uint16_t len, void *user)
{
    (void)user;
    memcpy(&s_ram[s_ram_len], data, len);
    s_ram_len += len;
    return E32P_UDS_NRC_OK;
}

static e32p_uds_nrc_t ram_end(void *user)
{
    (void)user;
    s_ram_crc = crc32_of(s_ram, s_ram_len);
    s_erased = false;   /* the next download needs a fresh erase */
    ESP_LOGI(TAG, "image received: %u bytes, CRC-32 %08X", (unsigned)s_ram_len, (unsigned)s_ram_crc);
    return E32P_UDS_NRC_OK;
}

static void ram_abort(void *user)
{
    (void)user;
    s_ram_len = 0u;
    s_ram_crc = 0u;   /* abandoned download: no stale "image complete" CRC for FF01 */
    s_erased = false;
}

static e32p_uds_sink_t s_ram_sink = {
    CONFIG_EX33_RAM_SINK_SIZE, ram_begin, ram_write, ram_end, ram_abort,
};

/**
 * @brief FF00 erase, FF01 check (option: the CRC-32 the tester expects, control 0x01 only).
 *        31 03 (results) before a start of that routine answers 0x24, request sequence error.
 */
static e32p_uds_nrc_t ram_routine(uint16_t rid, uint8_t control, const uint8_t *option,
                                  uint16_t option_len, uint8_t *status, uint16_t status_cap,
                                  uint16_t *status_len)
{
    if (rid == RID_ERASE) {
        if (control == UDS_RC_REQUEST_RESULTS && !s_erase_started) {
            return E32P_UDS_NRC_SEQUENCE_ERROR;
        }
        if (control == UDS_RC_REQUEST_RESULTS || s_erasing) {
            return E32P_UDS_NRC_CONDITIONS_NOT_CORRECT;
        }
        s_erasing = true;
        s_erased = false;
        if (xTaskCreate(ram_erase_worker, "ex33_erase", 3072, NULL, 4, NULL) != pdPASS) {
            s_erasing = false;
            return E32P_UDS_NRC_CONDITIONS_NOT_CORRECT;
        }
        s_erase_started = true;
        *status_len = 0u;
        return E32P_UDS_NRC_PENDING;
    }
    /* RID_CHECK: 00 = the image matches the CRC the tester sent, 01 = it does not;
       then the CRC this ECU computed, so the tester can see both. */
    if (control == UDS_RC_REQUEST_RESULTS && !s_check_started) {
        return E32P_UDS_NRC_SEQUENCE_ERROR;
    }
    if (status_cap < 5u || (control == 0x01u && option_len != 4u)) {
        return E32P_UDS_NRC_BAD_LENGTH;
    }
    if (control == 0x01u) {   /* the option record is only for a start */
        const uint32_t want = ((uint32_t)option[0] << 24) | ((uint32_t)option[1] << 16) |
                              ((uint32_t)option[2] << 8) | option[3];
        s_check_match = (want == s_ram_crc);
        s_check_started = true;
    }
    status[0] = s_check_match ? 0x00u : 0x01u;
    status[1] = (uint8_t)(s_ram_crc >> 24);
    status[2] = (uint8_t)(s_ram_crc >> 16);
    status[3] = (uint8_t)(s_ram_crc >> 8);
    status[4] = (uint8_t)s_ram_crc;
    *status_len = 5u;
    return E32P_UDS_NRC_OK;
}
#endif

/* ---- routines ----------------------------------------------------------------- */

static const e32p_uds_routine_t k_routines[] = {
    { RID_INJECT, E32P_UDS_IN_EXTENDED | E32P_UDS_IN_PROGRAMMING, false },
    { RID_ERASE,  E32P_UDS_IN_PROGRAMMING,                        true  },
    { RID_CHECK,  E32P_UDS_IN_PROGRAMMING,                        true  },
};

static e32p_uds_nrc_t routine(uint16_t rid, uint8_t control, const uint8_t *option,
                              uint16_t option_len, uint8_t *status, uint16_t status_cap,
                              uint16_t *status_len, void *user)
{
    if (rid == RID_INJECT) {
        if (control != 0x01u) {
            return E32P_UDS_NRC_SUBFUNC_NOT_SUPPORTED;
        }
        if (s_dtc_count == 0u) {
            s_dtc[0].dtc = 0x021700u;   /* P0217: engine over temperature */
            s_dtc[0].status = 0x09u;    /* failing now, confirmed         */
            s_dtc_count = 1u;
        }
        status[0] = 0x00u;
        *status_len = 1u;
        return E32P_UDS_NRC_OK;
    }
#if CONFIG_EX33_SINK_OTA
    return e32p_uds_ota_routine(rid, control, option, option_len, status, status_cap, status_len, user);
#else
    (void)user;
    return ram_routine(rid, control, option, option_len, status, status_cap, status_len);
#endif
}

/* ---- reset -------------------------------------------------------------------- */

static void on_reset(uint8_t type, void *user)
{
    (void)type;
    (void)user;
    ESP_LOGI(TAG, "ECU reset requested: restarting");
    /* The glue's send has finished, but the last frame may still sit in the TWAI
       transmit queue: give it a moment to reach the wire. */
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();
}

/* ---- the bus ------------------------------------------------------------------ */

#define BUS_CHECK_MS 100u

/**
 * @brief Report bus errors and recover from bus-off. TWAI does not come back from
 *        bus-off on its own: without this, one bad patch of wire would leave the
 *        ECU deaf until it is power-cycled. Same recovery as example 31.
 */
static void bus_keepalive(void)
{
    twai_status_info_t st;
    if (twai_get_status_info(&st) != ESP_OK) {
        return;
    }
    /* The error counters are the physical-layer diagnosis: a transmitter nobody
       acknowledges climbs TEC with REC at 0; a node seeing bits it cannot decode
       climbs REC and bus_err. */
    static uint32_t s_last_bus_err;
    if (st.bus_error_count != s_last_bus_err) {
        ESP_LOGW(TAG, "twai: state=%d tec=%lu rec=%lu bus_err=%lu tx_failed=%lu",
                 (int)st.state, (unsigned long)st.tx_error_counter,
                 (unsigned long)st.rx_error_counter, (unsigned long)st.bus_error_count,
                 (unsigned long)st.tx_failed_count);
        s_last_bus_err = st.bus_error_count;
    }
    if (st.state == TWAI_STATE_BUS_OFF) {
        ESP_LOGW(TAG, "bus-off: recovering");
        twai_initiate_recovery();
    } else if (st.state == TWAI_STATE_STOPPED) {
        twai_start();                  /* recovery finished; resume */
    }
}

/* ---- startup ------------------------------------------------------------------ */

void app_main(void)
{
    /* Seed source. Without Wi-Fi or Bluetooth running, the hardware RNG gives only
       pseudo-random numbers (ESP-IDF "Random Number Generation"). This turns the SAR ADC
       noise source back on, so the 0x27 seeds are random. IDF allows the call from app code
       before RF starts. This example uses no ADC, I2S, Wi-Fi or Bluetooth, so nothing
       conflicts; it is never switched off. Add bootloader_random_disable() first if one of
       those is ever added. */
    bootloader_random_enable();
    ESP_ERROR_CHECK(e32p_nvs_init());
    load_secret();
    int32_t setting = 0;
    if (e32p_nvs_read_i32(NVS_NS, NVS_SETTING, &setting) == ESP_OK) {
        s_setting = (uint32_t)setting;
    }
    e32p_ota_app_info_t app;
    if (e32p_ota_get_app_info(&app) == ESP_OK) {
        snprintf(s_sw_version, sizeof(s_sw_version), "%s", app.version);
    }

#if CONFIG_EX33_CONSOLE_PROVISIONING
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_0, 512, 0, 0, NULL, 0));
    xTaskCreate(console_task, "ex33_console", 3072, NULL, 3, NULL);
#endif

    e32p_twai_config_t bus = { .tx_gpio = CONFIG_EX33_CAN_TX_GPIO, .rx_gpio = CONFIG_EX33_CAN_RX_GPIO,
                               .bitrate_bps = E32P_ISOTP_DEFAULT_BITRATE,
                               .mode = E32P_TWAI_MODE_NORMAL };
    ESP_ERROR_CHECK(e32p_twai_init(&bus));
    ESP_ERROR_CHECK(e32p_twai_start());
    ESP_ERROR_CHECK(e32p_isotp_twai_start(NULL));

    e32p_uds_config_t server = {
        .dids = k_dids,
        .did_count = (uint8_t)(sizeof(k_dids) / sizeof(k_dids[0])),
        .on_did_read = did_read,
        .on_did_write = did_write,
        .on_seed = make_seed,
        .on_key = check_key,
        .on_sec_load = sec_load,
        .on_sec_store = sec_store,
        .dtc = &k_dtc,
        .routines = k_routines,
        .routine_count = (uint8_t)(sizeof(k_routines) / sizeof(k_routines[0])),
        .on_routine = routine,
        .on_reset = on_reset,
    };
#if CONFIG_EX33_SINK_OTA
    server.sink = e32p_uds_ota_sink();
    const char *sink_name = "ota";
#else
    s_ram = malloc(CONFIG_EX33_RAM_SINK_SIZE);
    ESP_ERROR_CHECK(s_ram != NULL ? ESP_OK : ESP_ERR_NO_MEM);
    server.sink = &s_ram_sink;
    const char *sink_name = "ram";
#endif
    const e32p_uds_isotp_config_t glue = { .server = &server, .ecu_addr = CONFIG_EX33_ECU_ADDR };
    ESP_ERROR_CHECK(e32p_uds_isotp_start(&glue));

    /* A freshly flashed image proves itself by getting this far with the bus up;
       without this the bootloader rolls back to the previous image. */
    if (e32p_ota_is_pending_verify()) {
        ESP_ERROR_CHECK(e32p_ota_mark_valid());
        ESP_LOGI(TAG, "new image confirmed");
    }
    printf("@EX33 READY slot=%s ver=%s sink=%s provisioned=%s rate=%u\n", e32p_ota_running_label(),
           s_sw_version, sink_name, have_secret() ? "yes" : "no", (unsigned)E32P_ISOTP_DEFAULT_BITRATE);

    for (;;) {
        bus_keepalive();
        vTaskDelay(pdMS_TO_TICKS(BUS_CHECK_MS));
    }
}
