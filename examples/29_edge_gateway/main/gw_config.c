/**
 * @file gw_config.c
 * @brief NVS-backed gateway config (see gw_config.h).
 */
#include "gw_config.h"
#include <string.h>
#include "e32p_nvs.h"

#define NS  "gwcfg"   /* NVS namespace */

/* Read a string key, or copy @p dflt if it is missing/short. */
static void get_str(e32p_nvs_handle_t h, const char *key, char *dst, size_t cap,
                    const char *dflt)
{
    size_t len = cap;
    if (e32p_nvs_get_str(h, key, dst, &len) != ESP_OK) {
        strncpy(dst, dflt, cap - 1);
        dst[cap - 1] = '\0';
    }
}

esp_err_t gw_config_load(gw_config_t *out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    strcpy(out->id, "gw-01");
    out->interval_s = 5;

    e32p_nvs_handle_t h = NULL;
    if (e32p_nvs_open(NS, E32P_NVS_READONLY, &h) != ESP_OK) {
        return ESP_OK;   /* no namespace yet -> defaults (unprovisioned) */
    }
    get_str(h, "ssid", out->ssid, sizeof(out->ssid), "");
    get_str(h, "pass", out->pass, sizeof(out->pass), "");
    get_str(h, "broker", out->broker, sizeof(out->broker), "");
    get_str(h, "id", out->id, sizeof(out->id), "gw-01");
    int32_t iv = 5;
    e32p_nvs_get_i32(h, "intvl", &iv);
    out->interval_s = (iv < 1) ? 5 : iv;
    e32p_nvs_close(h);
    return ESP_OK;
}

esp_err_t gw_config_save(const gw_config_t *cfg)
{
    if (!cfg) {
        return ESP_ERR_INVALID_ARG;
    }
    e32p_nvs_handle_t h = NULL;
    esp_err_t err = e32p_nvs_open(NS, E32P_NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    e32p_nvs_set_str(h, "ssid", cfg->ssid);
    e32p_nvs_set_str(h, "pass", cfg->pass);
    e32p_nvs_set_str(h, "broker", cfg->broker);
    e32p_nvs_set_str(h, "id", cfg->id[0] ? cfg->id : "gw-01");
    e32p_nvs_set_i32(h, "intvl", (cfg->interval_s < 1) ? 5 : cfg->interval_s);
    err = e32p_nvs_commit(h);
    e32p_nvs_close(h);
    return err;
}

bool gw_config_is_provisioned(const gw_config_t *cfg)
{
    return cfg && cfg->ssid[0] != '\0' && cfg->broker[0] != '\0';
}
