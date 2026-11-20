/**
 * @file e32p_pcnt.c
 * @brief e32p_pcnt implementation — pulse-counter facade over driver/pulse_cnt.h.
 */
#include <stdlib.h>
#include "esp_log.h"
#include "driver/pulse_cnt.h"
#include "driver/gpio.h"
#include "e32p_pcnt.h"

static const char *TAG = "e32p_pcnt";

struct e32p_pcnt_ctx {
    pcnt_unit_handle_t    unit;
    pcnt_channel_handle_t chan;
    bool                  running;
};

esp_err_t e32p_pcnt_init(const e32p_pcnt_config_t *cfg, e32p_pcnt_handle_t *out)
{
    E32P_CHECK_ARG(cfg != NULL, TAG);
    E32P_CHECK_ARG(out != NULL, TAG);
    E32P_CHECK_ARG(cfg->high_limit > 0 && cfg->low_limit < 0, TAG);
    E32P_CHECK_ARG(cfg->count_rising || cfg->count_falling, TAG);
    E32P_CHECK_ARG(GPIO_IS_VALID_GPIO(cfg->edge_gpio), TAG);

    struct e32p_pcnt_ctx *ctx = calloc(1, sizeof(*ctx));
    ESP_RETURN_ON_FALSE(ctx != NULL, ESP_ERR_NO_MEM, TAG, "alloc failed");

    esp_err_t ret = ESP_OK;
    const pcnt_unit_config_t uc = {
        .high_limit = cfg->high_limit,
        .low_limit  = cfg->low_limit,
    };
    E32P_GOTO_ON_ERR(pcnt_new_unit(&uc, &ctx->unit), TAG, fail_unit);

    const pcnt_chan_config_t cc = {
        .edge_gpio_num      = cfg->edge_gpio,
        .level_gpio_num     = -1,
        .flags.io_loop_back = cfg->io_loop_back,
    };
    E32P_GOTO_ON_ERR(pcnt_new_channel(ctx->unit, &cc, &ctx->chan), TAG, fail_chan);

    E32P_GOTO_ON_ERR(pcnt_channel_set_edge_action(ctx->chan,
        cfg->count_rising  ? PCNT_CHANNEL_EDGE_ACTION_INCREASE : PCNT_CHANNEL_EDGE_ACTION_HOLD,
        cfg->count_falling ? PCNT_CHANNEL_EDGE_ACTION_INCREASE : PCNT_CHANNEL_EDGE_ACTION_HOLD),
        TAG, fail_action);
    E32P_GOTO_ON_ERR(pcnt_channel_set_level_action(ctx->chan,
        PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_KEEP), TAG, fail_action);
    E32P_GOTO_ON_ERR(pcnt_unit_enable(ctx->unit), TAG, fail_action);

    *out = ctx;
    ESP_LOGI(TAG, "pcnt init gpio=%d limits=[%d,%d]", cfg->edge_gpio, cfg->low_limit, cfg->high_limit);
    return ESP_OK;

fail_action:
    pcnt_del_channel(ctx->chan);
fail_chan:
    pcnt_del_unit(ctx->unit);
fail_unit:
    free(ctx);
    return ret;
}

esp_err_t e32p_pcnt_deinit(e32p_pcnt_handle_t h)
{
    if (h == NULL) {
        return ESP_OK;
    }
    if (h->running) {
        pcnt_unit_stop(h->unit);
        h->running = false;
    }
    pcnt_unit_disable(h->unit);
    pcnt_del_channel(h->chan);
    pcnt_del_unit(h->unit);
    free(h);
    return ESP_OK;
}

esp_err_t e32p_pcnt_start(e32p_pcnt_handle_t h)
{
    E32P_CHECK_ARG(h != NULL, TAG);
    E32P_RETURN_ON_ERR(pcnt_unit_start(h->unit), TAG);
    h->running = true;
    return ESP_OK;
}

esp_err_t e32p_pcnt_stop(e32p_pcnt_handle_t h)
{
    E32P_CHECK_ARG(h != NULL, TAG);
    E32P_RETURN_ON_ERR(pcnt_unit_stop(h->unit), TAG);
    h->running = false;
    return ESP_OK;
}

esp_err_t e32p_pcnt_clear(e32p_pcnt_handle_t h)
{
    E32P_CHECK_ARG(h != NULL, TAG);
    return pcnt_unit_clear_count(h->unit);
}

esp_err_t e32p_pcnt_get_count(e32p_pcnt_handle_t h, int *out_count)
{
    E32P_CHECK_ARG(h != NULL, TAG);
    E32P_CHECK_ARG(out_count != NULL, TAG);
    return pcnt_unit_get_count(h->unit, out_count);
}
