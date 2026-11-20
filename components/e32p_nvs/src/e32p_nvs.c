/**
 * @file e32p_nvs.c
 * @brief e32p_nvs implementation — singleton init + namespace-handle facade.
 */
#include <stdlib.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "e32p_nvs.h"

static const char *TAG = "e32p_nvs";
static bool s_ready = false;

struct e32p_nvs_ctx {
    nvs_handle_t handle;
};

esp_err_t e32p_nvs_init(void)
{
    if (s_ready) {
        return ESP_OK;
    }
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "erasing NVS (%s)", esp_err_to_name(err));
        E32P_RETURN_ON_ERR(nvs_flash_erase(), TAG);
        err = nvs_flash_init();
    }
    E32P_RETURN_ON_ERR(err, TAG);
    s_ready = true;
    return ESP_OK;
}

esp_err_t e32p_nvs_deinit(void)
{
    if (!s_ready) {
        return ESP_OK;
    }
    E32P_RETURN_ON_ERR(nvs_flash_deinit(), TAG);
    s_ready = false;
    return ESP_OK;
}

bool e32p_nvs_is_not_found(esp_err_t err)
{
    return err == ESP_ERR_NVS_NOT_FOUND;
}

esp_err_t e32p_nvs_open(const char *namespace_name, e32p_nvs_mode_t mode,
                        e32p_nvs_handle_t *out)
{
    E32P_RETURN_ON_FALSE(s_ready, ESP_ERR_E32P_NOT_INIT, TAG, "call e32p_nvs_init first");
    E32P_CHECK_ARG(namespace_name != NULL, TAG);
    E32P_CHECK_ARG(out != NULL, TAG);

    struct e32p_nvs_ctx *ctx = calloc(1, sizeof(*ctx));
    ESP_RETURN_ON_FALSE(ctx != NULL, ESP_ERR_NO_MEM, TAG, "alloc failed");

    const nvs_open_mode_t m = (mode == E32P_NVS_READWRITE) ? NVS_READWRITE : NVS_READONLY;
    esp_err_t err = nvs_open(namespace_name, m, &ctx->handle);
    if (err != ESP_OK) {
        free(ctx);
        return err;
    }
    *out = ctx;
    return ESP_OK;
}

esp_err_t e32p_nvs_close(e32p_nvs_handle_t h)
{
    if (h == NULL) {
        return ESP_OK;
    }
    nvs_close(h->handle);
    free(h);
    return ESP_OK;
}

esp_err_t e32p_nvs_commit(e32p_nvs_handle_t h)
{
    E32P_CHECK_ARG(h != NULL, TAG);
    return nvs_commit(h->handle);
}

esp_err_t e32p_nvs_set_i32(e32p_nvs_handle_t h, const char *key, int32_t value)
{
    E32P_CHECK_ARG(h != NULL, TAG);
    E32P_CHECK_ARG(key != NULL, TAG);
    return nvs_set_i32(h->handle, key, value);
}

esp_err_t e32p_nvs_get_i32(e32p_nvs_handle_t h, const char *key, int32_t *out_value)
{
    E32P_CHECK_ARG(h != NULL, TAG);
    E32P_CHECK_ARG(key != NULL, TAG);
    E32P_CHECK_ARG(out_value != NULL, TAG);
    return nvs_get_i32(h->handle, key, out_value);
}

esp_err_t e32p_nvs_set_str(e32p_nvs_handle_t h, const char *key, const char *value)
{
    E32P_CHECK_ARG(h != NULL, TAG);
    E32P_CHECK_ARG(key != NULL, TAG);
    E32P_CHECK_ARG(value != NULL, TAG);
    return nvs_set_str(h->handle, key, value);
}

esp_err_t e32p_nvs_get_str(e32p_nvs_handle_t h, const char *key, char *out, size_t *inout_len)
{
    E32P_CHECK_ARG(h != NULL, TAG);
    E32P_CHECK_ARG(key != NULL, TAG);
    E32P_CHECK_ARG(inout_len != NULL, TAG);
    return nvs_get_str(h->handle, key, out, inout_len);
}

esp_err_t e32p_nvs_erase_key(e32p_nvs_handle_t h, const char *key)
{
    E32P_CHECK_ARG(h != NULL, TAG);
    E32P_CHECK_ARG(key != NULL, TAG);
    return nvs_erase_key(h->handle, key);
}

/* ---- One-shot helpers, built on the handle API ---- */

esp_err_t e32p_nvs_read_i32(const char *namespace_name, const char *key, int32_t *out_value)
{
    e32p_nvs_handle_t h = NULL;
    /* Open quietly: a missing namespace on read is a normal "no value yet"
       outcome (NVS_NOT_FOUND), not an error to log. Caller uses
       e32p_nvs_is_not_found() to branch. */
    esp_err_t err = e32p_nvs_open(namespace_name, E32P_NVS_READONLY, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = e32p_nvs_get_i32(h, key, out_value);
    e32p_nvs_close(h);
    return err;
}

esp_err_t e32p_nvs_write_i32(const char *namespace_name, const char *key, int32_t value)
{
    e32p_nvs_handle_t h = NULL;
    E32P_RETURN_ON_ERR(e32p_nvs_open(namespace_name, E32P_NVS_READWRITE, &h), TAG);
    esp_err_t err = e32p_nvs_set_i32(h, key, value);
    if (err == ESP_OK) {
        err = e32p_nvs_commit(h);
    }
    e32p_nvs_close(h);
    return err;
}

esp_err_t e32p_nvs_read_str(const char *namespace_name, const char *key, char *out, size_t *inout_len)
{
    e32p_nvs_handle_t h = NULL;
    /* Open quietly (see e32p_nvs_read_i32): missing namespace is not an error. */
    esp_err_t err = e32p_nvs_open(namespace_name, E32P_NVS_READONLY, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = e32p_nvs_get_str(h, key, out, inout_len);
    e32p_nvs_close(h);
    return err;
}

esp_err_t e32p_nvs_write_str(const char *namespace_name, const char *key, const char *value)
{
    e32p_nvs_handle_t h = NULL;
    E32P_RETURN_ON_ERR(e32p_nvs_open(namespace_name, E32P_NVS_READWRITE, &h), TAG);
    esp_err_t err = e32p_nvs_set_str(h, key, value);
    if (err == ESP_OK) {
        err = e32p_nvs_commit(h);
    }
    e32p_nvs_close(h);
    return err;
}
