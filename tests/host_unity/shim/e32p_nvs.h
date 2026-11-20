/**
 * @file e32p_nvs.h  (host stand-in)
 * @brief An in-memory key/value store, so persistence logic runs on the PC.
 *
 * Only the calls host-built code uses are here. The store is per translation unit
 * (the functions are static inline), which is fine as long as one component's
 * source file owns all its NVS traffic — true of e32p_obd2's DTC store. Real
 * flash and namespaces are covered on the target by the unit runner.
 */
#pragma once

#include <string.h>
#include "e32p_core.h"

#define SHIM_NVS_SLOTS 8
#define SHIM_NVS_VAL   96

static struct {
    char ns[24];
    char key[24];
    char val[SHIM_NVS_VAL];
    int  used;
} s_shim_nvs[SHIM_NVS_SLOTS] __attribute__((unused));

static inline esp_err_t e32p_nvs_init(void)
{
    return ESP_OK;   /* the host store needs no flash */
}

static inline bool e32p_nvs_is_not_found(esp_err_t err)
{
    return err == ESP_ERR_NOT_FOUND;
}

static inline esp_err_t e32p_nvs_write_str(const char *ns, const char *key, const char *value)
{
    if (ns == NULL || key == NULL || value == NULL || strlen(value) >= SHIM_NVS_VAL) {
        return ESP_ERR_INVALID_ARG;
    }
    for (int i = 0; i < SHIM_NVS_SLOTS; i++) {
        if (s_shim_nvs[i].used && strcmp(s_shim_nvs[i].ns, ns) == 0 &&
            strcmp(s_shim_nvs[i].key, key) == 0) {
            strcpy(s_shim_nvs[i].val, value);
            return ESP_OK;
        }
    }
    for (int i = 0; i < SHIM_NVS_SLOTS; i++) {
        if (!s_shim_nvs[i].used) {
            s_shim_nvs[i].used = 1;
            strncpy(s_shim_nvs[i].ns, ns, sizeof(s_shim_nvs[i].ns) - 1);
            strncpy(s_shim_nvs[i].key, key, sizeof(s_shim_nvs[i].key) - 1);
            strcpy(s_shim_nvs[i].val, value);
            return ESP_OK;
        }
    }
    return ESP_ERR_NO_MEM;
}

static inline esp_err_t e32p_nvs_read_str(const char *ns, const char *key, char *out, size_t *inout_len)
{
    if (ns == NULL || key == NULL || out == NULL || inout_len == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    for (int i = 0; i < SHIM_NVS_SLOTS; i++) {
        if (s_shim_nvs[i].used && strcmp(s_shim_nvs[i].ns, ns) == 0 &&
            strcmp(s_shim_nvs[i].key, key) == 0) {
            const size_t n = strlen(s_shim_nvs[i].val) + 1u;
            if (n > *inout_len) {
                return ESP_ERR_INVALID_ARG;
            }
            memcpy(out, s_shim_nvs[i].val, n);
            *inout_len = n;
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}
