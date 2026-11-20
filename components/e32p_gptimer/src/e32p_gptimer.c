/**
 * @file e32p_gptimer.c
 * @brief e32p_gptimer implementation — hardware timer + alarm facade.
 */
#include <stdlib.h>
#include "esp_attr.h"
#include "esp_log.h"
#include "driver/gptimer.h"
#include "e32p_gptimer.h"

static const char *TAG = "e32p_gptimer";

struct e32p_gptimer_ctx {
    gptimer_handle_t  timer;
    e32p_gptimer_cb_t cb;
    void             *arg;
    bool              running;
};

static bool IRAM_ATTR on_alarm(gptimer_handle_t timer,
                               const gptimer_alarm_event_data_t *edata, void *user)
{
    (void)timer;
    (void)edata;
    struct e32p_gptimer_ctx *ctx = (struct e32p_gptimer_ctx *)user;
    if (ctx->cb != NULL) {
        ctx->cb(ctx->arg);
    }
    return false;   /* no higher-priority task woken */
}

esp_err_t e32p_gptimer_init(const e32p_gptimer_config_t *cfg, e32p_gptimer_cb_t cb,
                            void *arg, e32p_gptimer_handle_t *out)
{
    E32P_CHECK_ARG(cfg != NULL, TAG);
    E32P_CHECK_ARG(out != NULL, TAG);
    E32P_CHECK_ARG(cfg->resolution_hz > 0, TAG);
    E32P_CHECK_ARG(cfg->alarm_period_ticks > 0, TAG);

    struct e32p_gptimer_ctx *ctx = calloc(1, sizeof(*ctx));
    ESP_RETURN_ON_FALSE(ctx != NULL, ESP_ERR_NO_MEM, TAG, "alloc failed");
    ctx->cb  = cb;
    ctx->arg = arg;

    const gptimer_config_t tc = {
        .clk_src       = GPTIMER_CLK_SRC_DEFAULT,
        .direction     = GPTIMER_COUNT_UP,
        .resolution_hz = cfg->resolution_hz,
    };
    esp_err_t ret = gptimer_new_timer(&tc, &ctx->timer);
    if (ret != ESP_OK) {
        free(ctx);
        return ret;
    }

    const gptimer_event_callbacks_t cbs = { .on_alarm = on_alarm };
    E32P_GOTO_ON_ERR(gptimer_register_event_callbacks(ctx->timer, &cbs, ctx), TAG, fail);

    const gptimer_alarm_config_t ac = {
        .alarm_count  = cfg->alarm_period_ticks,
        .reload_count = 0,
        .flags.auto_reload_on_alarm = cfg->auto_reload,
    };
    E32P_GOTO_ON_ERR(gptimer_set_alarm_action(ctx->timer, &ac), TAG, fail);
    E32P_GOTO_ON_ERR(gptimer_enable(ctx->timer), TAG, fail);

    *out = ctx;
    ESP_LOGI(TAG, "gptimer init res=%luHz period=%llu ticks reload=%d",
             (unsigned long)cfg->resolution_hz,
             (unsigned long long)cfg->alarm_period_ticks, cfg->auto_reload);
    return ESP_OK;

fail:
    gptimer_del_timer(ctx->timer);
    free(ctx);
    return ret;
}

esp_err_t e32p_gptimer_deinit(e32p_gptimer_handle_t h)
{
    if (h == NULL) {
        return ESP_OK;
    }
    if (h->running) {
        gptimer_stop(h->timer);
        h->running = false;
    }
    gptimer_disable(h->timer);
    gptimer_del_timer(h->timer);
    free(h);
    return ESP_OK;
}

esp_err_t e32p_gptimer_start(e32p_gptimer_handle_t h)
{
    E32P_CHECK_ARG(h != NULL, TAG);
    E32P_RETURN_ON_ERR(gptimer_start(h->timer), TAG);
    h->running = true;
    return ESP_OK;
}

esp_err_t e32p_gptimer_stop(e32p_gptimer_handle_t h)
{
    E32P_CHECK_ARG(h != NULL, TAG);
    E32P_RETURN_ON_ERR(gptimer_stop(h->timer), TAG);
    h->running = false;
    return ESP_OK;
}

esp_err_t e32p_gptimer_get_count(e32p_gptimer_handle_t h, uint64_t *out_ticks)
{
    E32P_CHECK_ARG(h != NULL, TAG);
    E32P_CHECK_ARG(out_ticks != NULL, TAG);
    return gptimer_get_raw_count(h->timer, out_ticks);
}
