/**
 * @file e32p_i2s.c
 * @brief e32p_i2s implementation — I2S standard-mode facade (one channel/handle).
 */
#include <stdlib.h>
#include "esp_log.h"
#include "driver/i2s_std.h"
#include "e32p_i2s.h"

static const char *TAG = "e32p_i2s";

struct e32p_i2s_ctx {
    i2s_chan_handle_t chan;
    e32p_i2s_dir_t    dir;
};

esp_err_t e32p_i2s_init(const e32p_i2s_config_t *cfg, e32p_i2s_handle_t *out)
{
    E32P_CHECK_ARG(cfg != NULL, TAG);
    E32P_CHECK_ARG(out != NULL, TAG);
    E32P_CHECK_ARG(cfg->port == 0 || cfg->port == 1, TAG);
    E32P_CHECK_ARG(cfg->bits == 16 || cfg->bits == 32, TAG);
    E32P_CHECK_ARG(cfg->sample_rate > 0, TAG);

    struct e32p_i2s_ctx *ctx = calloc(1, sizeof(*ctx));
    ESP_RETURN_ON_FALSE(ctx != NULL, ESP_ERR_NO_MEM, TAG, "alloc failed");
    ctx->dir = cfg->dir;

    const i2s_port_t port = (cfg->port == 1) ? I2S_NUM_1 : I2S_NUM_0;
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(port, I2S_ROLE_MASTER);

    esp_err_t err = (cfg->dir == E32P_I2S_TX)
                        ? i2s_new_channel(&chan_cfg, &ctx->chan, NULL)
                        : i2s_new_channel(&chan_cfg, NULL, &ctx->chan);
    if (err != ESP_OK) {
        free(ctx);
        return err;
    }

    /* Use locals (not constant ternaries) as macro args — the slot macro feeds
       its params into boolean contexts, which trips -Werror=int-in-bool-context
       when passed integer-constant ternaries directly. */
    const i2s_data_bit_width_t bw = (cfg->bits == 32) ? I2S_DATA_BIT_WIDTH_32BIT
                                                      : I2S_DATA_BIT_WIDTH_16BIT;
    const i2s_slot_mode_t sm = cfg->stereo ? I2S_SLOT_MODE_STEREO : I2S_SLOT_MODE_MONO;

    const i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(cfg->sample_rate),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(bw, sm),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = cfg->bclk_gpio,
            .ws   = cfg->ws_gpio,
            .dout = (cfg->dir == E32P_I2S_TX) ? cfg->data_gpio : I2S_GPIO_UNUSED,
            .din  = (cfg->dir == E32P_I2S_RX) ? cfg->data_gpio : I2S_GPIO_UNUSED,
        },
    };
    err = i2s_channel_init_std_mode(ctx->chan, &std_cfg);
    if (err == ESP_OK) {
        err = i2s_channel_enable(ctx->chan);
    }
    if (err != ESP_OK) {
        i2s_del_channel(ctx->chan);
        free(ctx);
        return err;
    }

    *out = ctx;
    ESP_LOGI(TAG, "i2s%d %s init: bclk=%d ws=%d data=%d %luHz %ubit %s", cfg->port,
             (cfg->dir == E32P_I2S_TX) ? "TX" : "RX", cfg->bclk_gpio, cfg->ws_gpio,
             cfg->data_gpio, (unsigned long)cfg->sample_rate, (unsigned)cfg->bits,
             cfg->stereo ? "stereo" : "mono");
    return ESP_OK;
}

esp_err_t e32p_i2s_deinit(e32p_i2s_handle_t h)
{
    if (h == NULL) {
        return ESP_OK;
    }
    i2s_channel_disable(h->chan);
    i2s_del_channel(h->chan);
    free(h);
    return ESP_OK;
}

esp_err_t e32p_i2s_write(e32p_i2s_handle_t h, const void *data, size_t len,
                         size_t *out_written, uint32_t timeout_ms)
{
    E32P_CHECK_ARG(h != NULL, TAG);
    E32P_CHECK_ARG(data != NULL, TAG);
    E32P_RETURN_ON_FALSE(h->dir == E32P_I2S_TX, ESP_ERR_INVALID_STATE, TAG, "channel is not TX");

    size_t written = 0;
    esp_err_t err = i2s_channel_write(h->chan, data, len, &written, timeout_ms);
    if (out_written != NULL) {
        *out_written = written;
    }
    return err;
}

esp_err_t e32p_i2s_read(e32p_i2s_handle_t h, void *buf, size_t len,
                        size_t *out_read, uint32_t timeout_ms)
{
    E32P_CHECK_ARG(h != NULL, TAG);
    E32P_CHECK_ARG(buf != NULL, TAG);
    E32P_RETURN_ON_FALSE(h->dir == E32P_I2S_RX, ESP_ERR_INVALID_STATE, TAG, "channel is not RX");

    size_t rd = 0;
    esp_err_t err = i2s_channel_read(h->chan, buf, len, &rd, timeout_ms);
    if (out_read != NULL) {
        *out_read = rd;
    }
    return err;
}
