/**
 * @file e32p_ble.c
 * @brief e32p_ble — pure advertising/UUID helpers (always built) + a NimBLE GATT
 *        peripheral (built only when CONFIG_BT_NIMBLE_ENABLED).
 */
#include <string.h>
#include "esp_log.h"
#include "e32p_ble.h"

/* ---- Pure helpers (always compiled) --------------------------------------- */

e32p_ble_uuid_t e32p_ble_uuid16(uint16_t uuid)
{
    e32p_ble_uuid_t u = { 0 };
    u.bytes[0] = (uint8_t)(uuid & 0xFF);
    u.bytes[1] = (uint8_t)(uuid >> 8);
    u.len = 2;
    return u;
}

e32p_ble_uuid_t e32p_ble_uuid128(const uint8_t bytes[16])
{
    e32p_ble_uuid_t u = { 0 };
    if (bytes != NULL) {
        memcpy(u.bytes, bytes, 16);
    }
    u.len = 16;
    return u;
}

int e32p_ble_adv_build(uint8_t *dst, size_t cap, uint8_t flags,
                       const char *name, const e32p_ble_uuid_t *svc)
{
    if (dst == NULL) {
        return -1;
    }
    size_t off = 0;

    /* Flags AD structure: len=2, type=0x01, value. */
    if (off + 3 > cap) {
        return -1;
    }
    dst[off++] = 0x02;
    dst[off++] = 0x01;
    dst[off++] = flags;

    /* Complete list of service UUIDs (16-bit type 0x03, 128-bit type 0x07). */
    if (svc != NULL) {
        if (svc->len == 2) {
            if (off + 4 > cap) {
                return -1;
            }
            dst[off++] = 0x03;
            dst[off++] = 0x03;
            dst[off++] = svc->bytes[0];
            dst[off++] = svc->bytes[1];
        } else if (svc->len == 16) {
            if (off + 18 > cap) {
                return -1;
            }
            dst[off++] = 0x11;
            dst[off++] = 0x07;
            memcpy(&dst[off], svc->bytes, 16);
            off += 16;
        } else {
            return -1;
        }
    }

    /* Complete Local Name (type 0x09). */
    if (name != NULL && name[0] != '\0') {
        size_t nl = strlen(name);
        if (off + 2 + nl > cap) {
            return -1;
        }
        dst[off++] = (uint8_t)(1 + nl);
        dst[off++] = 0x09;
        memcpy(&dst[off], name, nl);
        off += nl;
    }

    if (off > 31) {   /* legacy advertising limit */
        return -1;
    }
    return (int)off;
}

/* ==========================================================================
 * NimBLE peripheral — only when the Bluetooth host is compiled in.
 * ========================================================================== */
#if CONFIG_BT_NIMBLE_ENABLED

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

/* Provided by the bt component (store/config) — wires the bond store to NVS. */
void ble_store_config_init(void);

static const char *TAG = "e32p_ble";

static struct {
    bool     inited;
    uint8_t  addr_type;
    uint16_t conn_handle;
    e32p_ble_write_fn on_write;
    e32p_ble_conn_fn  on_conn;
    void    *user;
    char     name[32];
    uint8_t  adv_data[31];
    int      adv_len;
    size_t   n_chars;
    ble_uuid_any_t svc_uuid;
    struct {
        ble_uuid_any_t uuid;
        uint16_t       val_handle;
        uint8_t        val[E32P_BLE_VAL_MAX];
        uint16_t       val_len;
    } chr[E32P_BLE_MAX_CHARS];
    struct ble_gatt_chr_def chr_defs[E32P_BLE_MAX_CHARS + 1];
    struct ble_gatt_svc_def svc_defs[2];
} s;

static int gap_event(struct ble_gap_event *event, void *arg);

static int start_adv_ev(void)
{
    struct ble_gap_adv_params advp = { 0 };
    advp.conn_mode = BLE_GAP_CONN_MODE_UND;
    advp.disc_mode = BLE_GAP_DISC_MODE_GEN;
    ble_gap_adv_set_data(s.adv_data, s.adv_len);
    return ble_gap_adv_start(s.addr_type, NULL, BLE_HS_FOREVER, &advp, gap_event, NULL);
}

static int gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            s.conn_handle = event->connect.conn_handle;
            if (s.on_conn) {
                s.on_conn(true, s.user);
            }
        } else {
            start_adv_ev();   /* connection failed — keep advertising */
        }
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        s.conn_handle = BLE_HS_CONN_HANDLE_NONE;
        if (s.on_conn) {
            s.on_conn(false, s.user);
        }
        start_adv_ev();
        return 0;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        start_adv_ev();
        return 0;
    case BLE_GAP_EVENT_ENC_CHANGE:
        ESP_LOGI(TAG, "encryption change; status=%d", event->enc_change.status);
        return 0;
    case BLE_GAP_EVENT_REPEAT_PAIRING: {
        /* The central reconnected with a bond we no longer have. Drop the stale
           bond and let pairing start over — self-heals the "wrong PIN" case. */
        struct ble_gap_conn_desc desc;
        if (ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc) == 0) {
            ble_store_util_delete_peer(&desc.peer_id_addr);
        }
        return BLE_GAP_REPEAT_PAIRING_RETRY;
    }
    default:
        return 0;
    }
}

static int gatt_access(uint16_t conn_handle, uint16_t attr_handle,
                       struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    int idx = (int)(intptr_t)arg;
    switch (ctxt->op) {
    case BLE_GATT_ACCESS_OP_READ_CHR:
        return os_mbuf_append(ctxt->om, s.chr[idx].val, s.chr[idx].val_len) == 0
                   ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    case BLE_GATT_ACCESS_OP_WRITE_CHR: {
        uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
        if (len > E32P_BLE_VAL_MAX) {
            len = E32P_BLE_VAL_MAX;
        }
        uint16_t out_len = 0;
        ble_hs_mbuf_to_flat(ctxt->om, s.chr[idx].val, len, &out_len);
        s.chr[idx].val_len = out_len;
        if (s.on_write) {
            s.on_write((uint16_t)idx, s.chr[idx].val, out_len, s.user);
        }
        return 0;
    }
    default:
        return BLE_ATT_ERR_UNLIKELY;
    }
}

static void on_sync(void)
{
    ble_hs_id_infer_auto(0, &s.addr_type);
    start_adv_ev();
    ESP_LOGI(TAG, "advertising as \"%s\"", s.name);
}

static void host_task(void *param)
{
    (void)param;
    nimble_port_run();               /* returns on nimble_port_stop() */
    nimble_port_freertos_deinit();
}

static uint8_t chr_flags(uint8_t props)
{
    uint8_t f = 0;
    if (props & E32P_BLE_PROP_READ)   f |= BLE_GATT_CHR_F_READ;
    if (props & E32P_BLE_PROP_WRITE)  f |= BLE_GATT_CHR_F_WRITE;
    if (props & E32P_BLE_PROP_NOTIFY) f |= BLE_GATT_CHR_F_NOTIFY;
    return f;
}

esp_err_t e32p_ble_init(const e32p_ble_config_t *cfg)
{
    E32P_CHECK_ARG(cfg != NULL, TAG);
    E32P_CHECK_ARG(cfg->device_name != NULL, TAG);
    E32P_CHECK_ARG(cfg->n_chars <= E32P_BLE_MAX_CHARS, TAG);
    E32P_CHECK_ARG(cfg->n_chars == 0 || cfg->chars != NULL, TAG);

    memset(&s, 0, sizeof(s));
    s.conn_handle = BLE_HS_CONN_HANDLE_NONE;
    s.on_write = cfg->on_write;
    s.on_conn  = cfg->on_conn;
    s.user     = cfg->user;
    s.n_chars  = cfg->n_chars;
    snprintf(s.name, sizeof(s.name), "%s", cfg->device_name);

    int al = e32p_ble_adv_build(s.adv_data, sizeof(s.adv_data),
                                E32P_BLE_ADV_FLAGS_DEFAULT, s.name, &cfg->service_uuid);
    if (al < 0) {
        ESP_LOGE(TAG, "advertising payload too large (shorten the name)");
        return ESP_ERR_INVALID_ARG;
    }
    s.adv_len = al;

    for (size_t i = 0; i < cfg->n_chars; i++) {
        ble_uuid_init_from_buf(&s.chr[i].uuid, cfg->chars[i].uuid.bytes, cfg->chars[i].uuid.len);
        s.chr_defs[i].uuid = &s.chr[i].uuid.u;
        s.chr_defs[i].access_cb = gatt_access;
        s.chr_defs[i].arg = (void *)(intptr_t)i;
        s.chr_defs[i].flags = chr_flags(cfg->chars[i].props);
        s.chr_defs[i].val_handle = &s.chr[i].val_handle;
    }
    /* chr_defs[n_chars] stays zeroed (terminator). */
    ble_uuid_init_from_buf(&s.svc_uuid, cfg->service_uuid.bytes, cfg->service_uuid.len);
    s.svc_defs[0].type = BLE_GATT_SVC_TYPE_PRIMARY;
    s.svc_defs[0].uuid = &s.svc_uuid.u;
    s.svc_defs[0].characteristics = s.chr_defs;
    /* svc_defs[1] stays zeroed (terminator). */

    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init: %s", esp_err_to_name(err));
        return err;
    }
    ble_hs_cfg.sync_cb = on_sync;
    /* Bondable "Just Works": no display/keyboard, no MITM -> no PIN/passkey is
       ever shown. Bonding is allowed and the keys persist in NVS (via the store),
       so a central that insists on pairing succeeds silently. */
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

    ble_svc_gap_init();
    ble_svc_gatt_init();
    if (ble_gatts_count_cfg(s.svc_defs) != 0 || ble_gatts_add_svcs(s.svc_defs) != 0) {
        ESP_LOGE(TAG, "gatt service registration failed");
        nimble_port_deinit();
        return ESP_FAIL;
    }
    ble_svc_gap_device_name_set(s.name);
    ble_store_config_init();   /* bond store (NVS-backed) — pairing needs it */

    nimble_port_freertos_init(host_task);
    s.inited = true;
    return ESP_OK;
}

esp_err_t e32p_ble_deinit(void)
{
    if (!s.inited) {
        return ESP_OK;
    }
    ble_gap_adv_stop();
    nimble_port_stop();
    nimble_port_deinit();
    s.inited = false;
    return ESP_OK;
}

esp_err_t e32p_ble_start_adv(void)
{
    E32P_CHECK_ARG(s.inited, TAG);
    return start_adv_ev() == 0 ? ESP_OK : ESP_FAIL;
}

esp_err_t e32p_ble_stop_adv(void)
{
    E32P_CHECK_ARG(s.inited, TAG);
    ble_gap_adv_stop();
    return ESP_OK;
}

esp_err_t e32p_ble_set_value(uint16_t idx, const void *data, uint16_t len)
{
    E32P_CHECK_ARG(idx < s.n_chars, TAG);
    E32P_CHECK_ARG(data != NULL || len == 0, TAG);
    E32P_CHECK_ARG(len <= E32P_BLE_VAL_MAX, TAG);
    if (len) {
        memcpy(s.chr[idx].val, data, len);
    }
    s.chr[idx].val_len = len;
    return ESP_OK;
}

esp_err_t e32p_ble_notify(uint16_t idx, const void *data, uint16_t len)
{
    E32P_CHECK_ARG(idx < s.n_chars, TAG);
    E32P_CHECK_ARG(data != NULL || len == 0, TAG);
    E32P_CHECK_ARG(len <= E32P_BLE_VAL_MAX, TAG);
    if (len) {
        memcpy(s.chr[idx].val, data, len);
    }
    s.chr[idx].val_len = len;
    if (s.conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        return ESP_ERR_INVALID_STATE;
    }
    struct os_mbuf *om = ble_hs_mbuf_from_flat(s.chr[idx].val, len);
    if (om == NULL) {
        return ESP_ERR_NO_MEM;
    }
    return ble_gatts_notify_custom(s.conn_handle, s.chr[idx].val_handle, om) == 0 ? ESP_OK : ESP_FAIL;
}

bool e32p_ble_is_connected(void)
{
    return s.inited && s.conn_handle != BLE_HS_CONN_HANDLE_NONE;
}

#else  /* !CONFIG_BT_NIMBLE_ENABLED — radio not built in */

esp_err_t e32p_ble_init(const e32p_ble_config_t *cfg)   { (void)cfg; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t e32p_ble_deinit(void)                          { return ESP_OK; }
esp_err_t e32p_ble_start_adv(void)                       { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t e32p_ble_stop_adv(void)                        { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t e32p_ble_set_value(uint16_t i, const void *d, uint16_t l) { (void)i; (void)d; (void)l; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t e32p_ble_notify(uint16_t i, const void *d, uint16_t l)    { (void)i; (void)d; (void)l; return ESP_ERR_NOT_SUPPORTED; }
bool      e32p_ble_is_connected(void)                    { return false; }

#endif /* CONFIG_BT_NIMBLE_ENABLED */
