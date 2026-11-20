/**
 * @file e32p_foo.c
 * @brief TEMPLATE module implementation — canonical handle lifecycle + error style.
 */
#include <stdlib.h>
#include "esp_log.h"     /* include-what-you-use: this file calls ESP_LOGx directly */
#include "e32p_foo.h"

static const char *TAG = "e32p_foo";

struct e32p_foo_ctx {
    e32p_foo_config_t cfg;
    bool              running;
};

esp_err_t e32p_foo_init(const e32p_foo_config_t *cfg, e32p_foo_handle_t *out_handle)
{
    E32P_CHECK_ARG(cfg != NULL, TAG);
    E32P_CHECK_ARG(out_handle != NULL, TAG);

    struct e32p_foo_ctx *ctx = calloc(1, sizeof(*ctx));
    ESP_RETURN_ON_FALSE(ctx != NULL, ESP_ERR_NO_MEM, TAG, "alloc failed");

    ctx->cfg     = *cfg;
    ctx->running = false;

    /* Real modules acquire driver resources here, using E32P_RETURN_ON_ERR /
       E32P_GOTO_ON_ERR and freeing ctx on the error path. */

    *out_handle = ctx;
    ESP_LOGI(TAG, "init ok (pin=%d)", cfg->example_pin);
    return ESP_OK;
}

esp_err_t e32p_foo_deinit(e32p_foo_handle_t h)
{
    if (h == NULL) {
        return ESP_OK;   /* NULL-safe: deinit(NULL) is a no-op */
    }
    /* Real modules release driver resources here. */
    free(h);
    return ESP_OK;
}

esp_err_t e32p_foo_start(e32p_foo_handle_t h)
{
    E32P_CHECK_ARG(h != NULL, TAG);
    h->running = true;
    return ESP_OK;
}

esp_err_t e32p_foo_stop(e32p_foo_handle_t h)
{
    E32P_CHECK_ARG(h != NULL, TAG);
    h->running = false;
    return ESP_OK;
}

esp_err_t e32p_foo_get_status(e32p_foo_handle_t h, e32p_foo_status_t *out)
{
    E32P_CHECK_ARG(h != NULL, TAG);
    E32P_CHECK_ARG(out != NULL, TAG);
    out->running = h->running;
    return ESP_OK;
}
