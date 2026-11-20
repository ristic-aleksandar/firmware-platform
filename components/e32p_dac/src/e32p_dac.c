/**
 * @file e32p_dac.c
 * @brief e32p_dac implementation — 8-bit oneshot DAC facade.
 */
#include <stdlib.h>
#include "esp_log.h"
#include "driver/dac_oneshot.h"
#include "e32p_dac.h"

static const char *TAG = "e32p_dac";

struct e32p_dac_ctx {
    dac_oneshot_handle_t dac;
};

static dac_channel_t map_chan(int ch)
{
    return (ch == 1) ? DAC_CHAN_1 : DAC_CHAN_0;
}

esp_err_t e32p_dac_init(int channel, e32p_dac_handle_t *out)
{
    E32P_CHECK_ARG(out != NULL, TAG);
    E32P_CHECK_ARG(channel == 0 || channel == 1, TAG);

    struct e32p_dac_ctx *ctx = calloc(1, sizeof(*ctx));
    ESP_RETURN_ON_FALSE(ctx != NULL, ESP_ERR_NO_MEM, TAG, "alloc failed");

    const dac_oneshot_config_t cfg = { .chan_id = map_chan(channel) };
    esp_err_t err = dac_oneshot_new_channel(&cfg, &ctx->dac);
    if (err != ESP_OK) {
        free(ctx);
        return err;
    }
    *out = ctx;
    ESP_LOGI(TAG, "dac ch%d init (GPIO%d)", channel, (channel == 1) ? 26 : 25);
    return ESP_OK;
}

esp_err_t e32p_dac_deinit(e32p_dac_handle_t h)
{
    if (h == NULL) {
        return ESP_OK;
    }
    dac_oneshot_del_channel(h->dac);
    free(h);
    return ESP_OK;
}

esp_err_t e32p_dac_set_raw(e32p_dac_handle_t h, uint8_t value)
{
    E32P_CHECK_ARG(h != NULL, TAG);
    return dac_oneshot_output_voltage(h->dac, value);
}

esp_err_t e32p_dac_set_voltage(e32p_dac_handle_t h, uint32_t mv)
{
    E32P_CHECK_ARG(h != NULL, TAG);
    if (mv > 3300) {
        mv = 3300;
    }
    return e32p_dac_set_raw(h, (uint8_t)((mv * 255u) / 3300u));
}
