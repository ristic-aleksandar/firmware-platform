/**
 * @file e32p_obd2_dtc.c
 * @brief Diagnostic trouble code store, NVS-backed.
 *
 * Codes live in a small fixed array and are written to one short NVS string —
 * `"0133C,0420P"`, four hex digits per code plus C for confirmed or P for
 * pending. One write when a code is set, one when the store is cleared, so the
 * flash sees a handful of writes rather than a stream.
 *
 * The freeze frame is the values mode 0x01 would have returned when the first
 * code was stored. It is **not** persisted: after a reset the codes come back,
 * the snapshot does not, and mode 0x02 answers "not supported" until a new code
 * is stored. Persisting a snapshot of live sensor values would be pretending to
 * know what the engine was doing before the reset.
 */
#include <string.h>
#include "e32p_nvs.h"
#include "e32p_obd2.h"
#include "e32p_obd2_priv.h"

static const char *TAG = "e32p_obd2";

#define DTC_KEY      "dtcs"
#define DTC_TEXT_LEN 6u   /* "0133C," */
#define DTC_BUF_LEN  (E32P_OBD2_MAX_DTC * DTC_TEXT_LEN + 1u)

static char hex_digit(uint8_t nibble)
{
    return (nibble < 10u) ? (char)('0' + (int)nibble) : (char)('A' + (int)nibble - 10);
}

static bool hex_value(char ch, uint8_t *out)
{
    if (ch >= '0' && ch <= '9') { *out = (uint8_t)(ch - '0');        return true; }
    if (ch >= 'A' && ch <= 'F') { *out = (uint8_t)(ch - 'A' + 10);   return true; }
    if (ch >= 'a' && ch <= 'f') { *out = (uint8_t)(ch - 'a' + 10);   return true; }
    return false;
}

/** @brief Find @p dtc in the store; returns its index or -1. */
static int find_dtc(const struct e32p_obd2_ctx *c, uint16_t dtc)
{
    for (uint8_t i = 0; i < c->dtc_count; i++) {
        if (c->dtc[i].code == dtc) {
            return (int)i;
        }
    }
    return -1;
}

/** @brief Snapshot every table PID's current value, plus the code that caused it. */
static void capture_freeze(struct e32p_obd2_ctx *c, uint16_t dtc)
{
    if (c->freeze_data == NULL) {
        return;
    }
    memset(c->freeze_data, 0, (size_t)c->cfg.pid_count * E32P_OBD2_PID_DATA_MAX);
    for (uint8_t i = 0; i < c->cfg.pid_count; i++) {
        const e32p_obd2_pid_t *p = &c->cfg.pids[i];
        uint8_t *slot = &c->freeze_data[(size_t)i * E32P_OBD2_PID_DATA_MAX];
        if (p->pid == 0x1Cu) {
            slot[0] = c->cfg.obd_standard;
        } else if (c->cfg.on_read(p->pid, slot, p->len, c->cfg.user) != ESP_OK) {
            memset(slot, 0, E32P_OBD2_PID_DATA_MAX);
        }
    }
    c->freeze_dtc = dtc;
    c->freeze_valid = true;
}

bool e32p_obd2_freeze_get(const struct e32p_obd2_ctx *c, uint8_t pid, uint8_t *out, uint8_t len)
{
    if (!c->freeze_valid || c->freeze_data == NULL) {
        return false;
    }
    for (uint8_t i = 0; i < c->cfg.pid_count; i++) {
        if (c->cfg.pids[i].pid == pid) {
            memcpy(out, &c->freeze_data[(size_t)i * E32P_OBD2_PID_DATA_MAX], len);
            return true;
        }
    }
    return false;
}

esp_err_t e32p_obd2_dtc_save(struct e32p_obd2_ctx *c)
{
    E32P_CHECK_ARG(c != NULL, TAG);
    if (c->cfg.nvs_namespace == NULL) {
        return ESP_OK;                       /* RAM-only store, nothing to write */
    }
    char buf[DTC_BUF_LEN];
    size_t n = 0;
    for (uint8_t i = 0; i < c->dtc_count; i++) {
        if (n != 0u) {
            buf[n++] = ',';
        }
        buf[n++] = hex_digit((uint8_t)((c->dtc[i].code >> 12) & 0x0Fu));
        buf[n++] = hex_digit((uint8_t)((c->dtc[i].code >> 8) & 0x0Fu));
        buf[n++] = hex_digit((uint8_t)((c->dtc[i].code >> 4) & 0x0Fu));
        buf[n++] = hex_digit((uint8_t)(c->dtc[i].code & 0x0Fu));
        buf[n++] = c->dtc[i].confirmed ? 'C' : 'P';
    }
    buf[n] = '\0';
    return e32p_nvs_write_str(c->cfg.nvs_namespace, DTC_KEY, buf);
}

esp_err_t e32p_obd2_dtc_load(struct e32p_obd2_ctx *c)
{
    E32P_CHECK_ARG(c != NULL, TAG);
    c->dtc_count = 0;
    if (c->cfg.nvs_namespace == NULL) {
        return ESP_OK;
    }
    char buf[DTC_BUF_LEN];
    size_t len = sizeof(buf);
    const esp_err_t err = e32p_nvs_read_str(c->cfg.nvs_namespace, DTC_KEY, buf, &len);
    if (e32p_nvs_is_not_found(err)) {
        return ESP_OK;                       /* nothing stored yet: not an error */
    }
    if (err != ESP_OK) {
        return err;
    }

    /* Parse "0133C,0420P". A damaged string costs the codes, not the boot: stop
       at the first entry that does not parse and keep what came before it. */
    for (size_t i = 0; buf[i] != '\0' && c->dtc_count < E32P_OBD2_MAX_DTC; ) {
        uint8_t nib[4];
        if (!hex_value(buf[i], &nib[0]) || !hex_value(buf[i + 1], &nib[1]) ||
            !hex_value(buf[i + 2], &nib[2]) || !hex_value(buf[i + 3], &nib[3])) {
            break;
        }
        const char flag = buf[i + 4];
        if (flag != 'C' && flag != 'P') {
            break;
        }
        c->dtc[c->dtc_count].code = (uint16_t)(((uint16_t)nib[0] << 12) |
                                               ((uint16_t)nib[1] << 8) |
                                               ((uint16_t)nib[2] << 4) | nib[3]);
        c->dtc[c->dtc_count].confirmed = (flag == 'C');
        c->dtc_count++;
        i += 5u;
        if (buf[i] == ',') {
            i++;
        }
    }
    ESP_LOGI(TAG, "dtc store loaded: %u code(s)", (unsigned)c->dtc_count);
    return ESP_OK;
}

esp_err_t e32p_obd2_dtc_set(e32p_obd2_handle_t h, uint16_t dtc, bool confirmed)
{
    E32P_CHECK_ARG(h != NULL, TAG);

    const int at = find_dtc(h, dtc);
    if (at >= 0) {
        if (h->dtc[at].confirmed == confirmed) {
            return ESP_OK;                   /* already known: no flash write */
        }
        h->dtc[at].confirmed = confirmed;    /* pending -> confirmed, or back  */
    } else {
        if (h->dtc_count >= E32P_OBD2_MAX_DTC) {
            return ESP_ERR_NO_MEM;
        }
        if (h->dtc_count == 0u) {
            capture_freeze(h, dtc);          /* the first code freezes the frame */
        }
        h->dtc[h->dtc_count].code = dtc;
        h->dtc[h->dtc_count].confirmed = confirmed;
        h->dtc_count++;
    }
    return e32p_obd2_dtc_save(h);
}

esp_err_t e32p_obd2_dtc_clear(e32p_obd2_handle_t h)
{
    E32P_CHECK_ARG(h != NULL, TAG);
    h->dtc_count = 0;
    h->freeze_valid = false;
    h->freeze_dtc = 0;
    return e32p_obd2_dtc_save(h);            /* writes an empty string */
}

uint8_t e32p_obd2_dtc_count(e32p_obd2_handle_t h, bool confirmed)
{
    if (h == NULL) {
        return 0u;
    }
    uint8_t n = 0;
    for (uint8_t i = 0; i < h->dtc_count; i++) {
        if (h->dtc[i].confirmed == confirmed) {
            n++;
        }
    }
    return n;
}

esp_err_t e32p_obd2_dtc_get(e32p_obd2_handle_t h, uint8_t index, uint16_t *dtc, bool *confirmed)
{
    E32P_CHECK_ARG(h != NULL && dtc != NULL && confirmed != NULL, TAG);
    if (index >= h->dtc_count) {
        return ESP_ERR_NOT_FOUND;
    }
    *dtc = h->dtc[index].code;
    *confirmed = h->dtc[index].confirmed;
    return ESP_OK;
}
