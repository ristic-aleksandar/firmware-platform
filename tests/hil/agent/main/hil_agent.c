/**
 * @file hil_agent.c
 * @brief HIL test-agent core — role, command dispatcher, test registry.
 */
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "e32p_sysinfo.h"
#include "e32p_nvs.h"
#include "hil_agent.h"
#include "hil_can.h"
#include "hil_sys.h"
#include "hil_periph.h"
#include "hil_bus.h"
#include "hil_wireless.h"
#include "hil_sensor.h"
#include "hil_pm.h"
#include "hil_mqtt.h"
#include "hil_modbus.h"
#include "hil_ota.h"
#include "hil_net.h"
#include "hil_ble.h"
#include "hil_diag.h"

#define HIL_UART      UART_NUM_0
#define HIL_OUT_LOCK_MS 100   /* cap on waiting for the console lock */
#define HIL_NVS_NS    "hil"
#define HIL_NVS_KEY   "role"

typedef enum {
    HIL_ROLE_UNSET = 0,
    HIL_ROLE_DUT = 1,
    HIL_ROLE_COMPANION = 2,
} hil_role_t;

static hil_role_t s_role = HIL_ROLE_UNSET;

static const char *role_str(hil_role_t r)
{
    switch (r) {
    case HIL_ROLE_DUT:       return "DUT";
    case HIL_ROLE_COMPANION: return "COMPANION";
    default:                 return "UNSET";
    }
}

/* Protocol lines and ESP_LOG output share UART0 but reach it by different routes:
   hil_emit() writes through the UART driver, ESP_LOG through vprintf/stdout. Without
   a common lock a log line can land inside a protocol line - seen on the companion as
   "EVT ISOTP peer ready" with the leading @ eaten. Both routes take this lock, so a
   line is whole or absent. It never waits forever and never drops output: if the take
   fails the line still goes out, because a mangled line beats a missing one. */
static SemaphoreHandle_t s_out_lock;

static bool out_lock(void)
{
    if ((s_out_lock == NULL) || xPortInIsrContext()) {
        return false;
    }
    return xSemaphoreTakeRecursive(s_out_lock, pdMS_TO_TICKS(HIL_OUT_LOCK_MS)) == pdTRUE;
}

static void out_unlock(bool locked)
{
    if (locked) {
        (void)xSemaphoreGiveRecursive(s_out_lock);
    }
}

/** @brief ESP_LOG sink: the stock vprintf, serialised against hil_emit().
 *
 * @param[in] fmt  printf-style format from the logging macro.
 * @param[in] ap   its argument list.
 * @return Characters written, as vprintf() reports them.
 */
static int hil_log_vprintf(const char *fmt, va_list ap)
{
    const bool locked = out_lock();
    const int n = vprintf(fmt, ap);
    /* Flushed inside the lock: an unflushed tail would otherwise surface later,
       in the middle of whatever is written next. */
    (void)fflush(stdout);
    out_unlock(locked);
    return n;
}

void hil_out_init(void)
{
    if (s_out_lock != NULL) {
        return;
    }
    s_out_lock = xSemaphoreCreateRecursiveMutex();
    if (s_out_lock != NULL) {
        (void)esp_log_set_vprintf(hil_log_vprintf);
    }
}

void hil_emit(const char *fmt, ...)
{
    char buf[224];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf) - 2, fmt, ap);
    va_end(ap);
    if (n < 0) {
        return;
    }
    if (n > (int)sizeof(buf) - 2) {
        n = sizeof(buf) - 2;
    }
    buf[n++] = '\r';
    buf[n++] = '\n';
    const bool locked = out_lock();
    uart_write_bytes(HIL_UART, buf, n);
    out_unlock(locked);
}

/* --- Test registry ------------------------------------------------------- */

typedef struct {
    const char *id;
    hil_test_fn fn;
} hil_test_t;

static hil_verdict_t test_ping(char *detail, size_t n)
{
    snprintf(detail, n, "pong");
    return HIL_PASS;
}

static hil_verdict_t test_sysinfo(char *detail, size_t n)
{
    e32p_sysinfo_t si;
    if (e32p_sysinfo_get(&si) != ESP_OK) {
        snprintf(detail, n, "sysinfo get failed");
        return HIL_FAIL;
    }
    if (si.cores < 1 || si.chip_model[0] == '\0') {
        snprintf(detail, n, "implausible snapshot");
        return HIL_FAIL;
    }
    snprintf(detail, n, "chip=%s cores=%d", si.chip_model, si.cores);
    return HIL_PASS;
}

static const hil_test_t s_tests[] = {
    { "ping",             test_ping },
    { "sysinfo",          test_sysinfo },
    /* CAN / J1939 — single board (no bus needed) */
    { "can_twai_neg",     hil_can_twai_neg },
    { "can_twai_selftx",  hil_can_twai_selftx },
    { "can_j1939_codec",  hil_can_j1939_codec },
    { "can_j1939_neg",    hil_can_j1939_neg },
    { "can_tp_loop",      hil_can_tp_loop },
    { "can_tp_neg",       hil_can_tp_neg },
    /* CAN / J1939 — two boards + transceivers */
    { "can_twai_xfer",    hil_can_twai_xfer },
    { "can_j1939_single", hil_can_j1939_single },
    { "can_j1939_rx",     hil_can_j1939_rx },
    { "can_tp_bam",       hil_can_tp_bam },
    { "can_tp_cmdt",      hil_can_tp_cmdt },
    { "can_stress",       hil_can_stress },
    { "can_acl_contend",  hil_can_acl_contend },
    /* Foundation — single board */
    { "sys_event",        hil_sys_event },
    { "sys_nvs",          hil_sys_nvs },
    { "sys_wdt",          hil_sys_wdt },
    /* GPIO / analog / timers — single board */
    { "periph_gpio",      hil_periph_gpio },
    { "periph_ledc",      hil_periph_ledc },
    { "periph_adc",       hil_periph_adc },
    { "periph_dac",       hil_periph_dac },
    { "periph_touch",     hil_periph_touch },
    { "periph_pcnt",      hil_periph_pcnt },
    { "periph_gptimer",   hil_periph_gptimer },
    /* Buses — single board (uart_xfer needs a companion) */
    { "bus_uart_loop",    hil_bus_uart_loop },
    { "bus_uart_xfer",    hil_bus_uart_xfer },
    { "bus_i2c_bus",      hil_bus_i2c_bus },
    { "bus_spi_loop",     hil_bus_spi_loop },
    { "bus_i2s_tx",       hil_bus_i2s_tx },
    { "bus_ws2812",       hil_bus_ws2812 },
    /* Wireless — arg validation single board; espnow_xfer needs a companion */
    { "wl_wifi_neg",      hil_wl_wifi_neg },
    { "wl_espnow_neg",    hil_wl_espnow_neg },
    { "wl_espnow_xfer",   hil_wl_espnow_xfer },
    /* Device drivers — need the physical part wired (host: --with-sensors) */
    { "sensor_bmp280",    hil_sensor_bmp280 },
    { "sensor_tm1637",    hil_sensor_tm1637 },
    /* Low-power — single board, non-disruptive (deep sleep is in the 22_pm demo) */
    { "pm_wakecause",     hil_pm_wakecause },
    { "pm_lightsleep",    hil_pm_lightsleep },
    /* MQTT — single board self-test (topic matching + client alloc; no broker) */
    { "mqtt_selftest",    hil_mqtt_selftest },
    /* Modbus — single board self-test (protocol round-trip; no wire) */
    { "modbus_selftest",  hil_modbus_selftest },
    /* Modbus — two boards: real RTU transport over a crossed UART2 link */
    { "modbus_rtu_xfer",  hil_modbus_rtu_xfer },
    /* OTA — single board query self-test (slots/version/rollback; non-destructive) */
    { "ota_query",        hil_ota_query },
    /* BLE — single board self-test (adv payload + UUID; no radio) */
    { "ble_selftest",     hil_ble_selftest },
    /* ISO-TP - single board + transceiver (self-reception loop, no companion) */
    { "isotp_self",       hil_diag_isotp_self },
    { "isotp_lifecycle",  hil_diag_isotp_lifecycle },
    /* ISO-TP - two boards: against the companion's ISO-TP peer */
    { "isotp_sf",         hil_diag_isotp_sf },
    { "isotp_mf",         hil_diag_isotp_mf },
    { "isotp_big",        hil_diag_isotp_big },
    { "isotp_fcwait",     hil_diag_isotp_fcwait },
    /* OBD-II - two boards: against the companion's OBD-II server */
    { "obd2_pid_phys",    hil_diag_obd2_pid_phys },
    { "obd2_pid_func",    hil_diag_obd2_pid_func },
    { "obd2_unsupported", hil_diag_obd2_unsupported },
    { "obd2_vin",         hil_diag_obd2_vin },
    { "obd2_dtc_cycle",   hil_diag_obd2_dtc_cycle },
    { "obd2_freeze",      hil_diag_obd2_freeze },
    { "obd2_dtc_seed",    hil_diag_obd2_seed },   /* runs on the COMPANION */
    { "isotp_ncr",        hil_diag_isotp_ncr },
    /* UDS - two boards: against the companion's UDS server */
    { "uds_session",      hil_diag_uds_session },
    { "uds_functional",   hil_diag_uds_functional },
    { "uds_security",     hil_diag_uds_security },
    { "uds_did_write",    hil_diag_uds_did_write },
    { "uds_dtc",          hil_diag_uds_dtc },
    { "uds_erase",        hil_diag_uds_erase },
    { "uds_download",     hil_diag_uds_download },
    { "uds_lockout",      hil_diag_uds_lockout },
    { "uds_reset",        hil_diag_uds_reset },        /* the companion restarts */
    { "uds_after_reset",  hil_diag_uds_after_reset },
    { "uds_sec_reset",    hil_diag_uds_sec_reset },    /* the companion restarts */
    { "uds_sec_after",    hil_diag_uds_sec_after_reset },
};
static const size_t s_ntests = sizeof(s_tests) / sizeof(s_tests[0]);

/* --- Role persistence ---------------------------------------------------- */

static void role_load(void)
{
    int32_t v = 0;
    esp_err_t err = e32p_nvs_read_i32(HIL_NVS_NS, HIL_NVS_KEY, &v);
    s_role = (err == ESP_OK) ? (hil_role_t)v : HIL_ROLE_UNSET;
}

bool hil_agent_is_companion(void)
{
    return s_role == HIL_ROLE_COMPANION;
}

/* --- Command handlers ---------------------------------------------------- */

static void cmd_run(const char *id)
{
    for (size_t i = 0; i < s_ntests; i++) {
        if (strcmp(id, s_tests[i].id) == 0) {
            char detail[128] = {0};
            uint32_t heap0 = esp_get_free_heap_size();
            int64_t t0 = esp_timer_get_time();
            hil_verdict_t v = s_tests[i].fn(detail, sizeof(detail));
            int64_t t1 = esp_timer_get_time();
            int32_t dheap = (int32_t)(esp_get_free_heap_size() - heap0);
            long ms = (long)((t1 - t0) / 1000);
            hil_emit("@RSP RUN %s %s heap=%+d t=%ldms detail=%s", id,
                     (v == HIL_PASS) ? "PASS" : "FAIL", (int)dheap, ms, detail);
            return;
        }
    }
    hil_emit("@ERR unknown test: %s", id);
}

/**
 * @brief Copy @p src into @p dst, rendering non-printable bytes as \xNN.
 *
 * @param[in]  src  NUL-terminated source line.
 * @param[out] dst  Destination buffer; always NUL-terminated.
 * @param[in]  n    Size of @p dst in bytes.
 */
static void escape_line(const char *src, char *dst, size_t n)
{
    size_t o = 0;
    for (size_t i = 0; (src[i] != '\0') && ((o + 5u) < n); i++) {
        unsigned char c = (unsigned char)src[i];
        if ((c >= 0x20u) && (c < 0x7Fu)) {
            dst[o++] = (char)c;
        } else {
            int w = snprintf(dst + o, n - o, "\\x%02X", c);
            if (w <= 0) {
                break;
            }
            o += (size_t)w;
        }
    }
    dst[o] = '\0';
}

void hil_agent_dispatch(char *line)
{
    /* Kept before strtok(), which cuts the line at its first space: a line that
       is not a command is only useful to the harness if it can be seen whole.
       A stale partial line in the reader's buffer prefixes the next command and
       lands here - see tests/hil/BASELINE.md. */
    char raw[96];
    escape_line(line, raw, sizeof(raw));

    char *tok = strtok(line, " ");
    if (tok == NULL) {
        return;
    }
    if (strcmp(tok, "@CMD") != 0) {
        hil_emit("@ERR expected @CMD prefix, got: %s", raw);
        return;
    }
    char *verb = strtok(NULL, " ");
    if (verb == NULL) {
        hil_emit("@ERR empty command");
        return;
    }
    char *arg = strtok(NULL, " ");

    if (strcmp(verb, "PING") == 0) {
        hil_emit("@RSP PING OK");
    } else if (strcmp(verb, "ROLE") == 0) {
        hil_emit("@RSP ROLE %s", role_str(s_role));
    } else if (strcmp(verb, "SETROLE") == 0) {
        if (arg == NULL) {
            hil_emit("@ERR SETROLE needs a role");
            return;
        }
        hil_role_t r = HIL_ROLE_UNSET;
        if (strcmp(arg, "DUT") == 0) {
            r = HIL_ROLE_DUT;
        } else if (strcmp(arg, "COMPANION") == 0) {
            r = HIL_ROLE_COMPANION;
        } else {
            hil_emit("@ERR bad role: %s", arg);
            return;
        }
        if (e32p_nvs_write_i32(HIL_NVS_NS, HIL_NVS_KEY, (int32_t)r) == ESP_OK) {
            s_role = r;
            hil_emit("@RSP SETROLE OK");
        } else {
            hil_emit("@ERR SETROLE nvs write failed");
        }
    } else if (strcmp(verb, "SYSINFO") == 0) {
        e32p_sysinfo_t si;
        if (e32p_sysinfo_get(&si) != ESP_OK) {
            hil_emit("@ERR sysinfo failed");
            return;
        }
        hil_emit("@RSP SYSINFO chip=%s cores=%d heap=%u uptime=%llums reset=%s",
                 si.chip_model, si.cores, (unsigned)esp_get_free_heap_size(),
                 (unsigned long long)e32p_sysinfo_uptime_ms(),
                 e32p_sysinfo_reset_reason_str());
    } else if (strcmp(verb, "LIST") == 0) {
        /* One line per test (the list is long); terminated by an END line. */
        for (size_t i = 0; i < s_ntests; i++) {
            hil_emit("@RSP LIST %s", s_tests[i].id);
        }
        hil_emit("@RSP LIST END");
    } else if (strcmp(verb, "RUN") == 0) {
        if (arg == NULL) {
            hil_emit("@ERR RUN needs a test id");
            return;
        }
        cmd_run(arg);
    } else if (strcmp(verb, "WIFI") == 0) {
        /* @CMD WIFI <ssid> [pass] — ssid/pass are assumed space-free. */
        if (arg == NULL) {
            hil_emit("@ERR WIFI needs an ssid");
            return;
        }
        char *pass = strtok(NULL, " ");
        hil_net_wifi(arg, pass);
    } else if (strcmp(verb, "MQTT") == 0) {
        if (arg == NULL) {
            hil_emit("@ERR MQTT needs a broker ip");
            return;
        }
        hil_net_mqtt(arg);
    } else if (strcmp(verb, "OTA") == 0) {
        if (arg == NULL) {
            hil_emit("@ERR OTA needs a url");
            return;
        }
        hil_net_ota(arg);   /* reboots on success */
    } else if (strcmp(verb, "OTASLOT") == 0) {
        hil_net_running_slot();
    } else {
        hil_emit("@ERR unknown command: %s", verb);
    }
}

void hil_agent_init(void)
{
    role_load();
    hil_emit("@EVT READY role=%s fw=hil-agent", role_str(s_role));
}
