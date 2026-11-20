/**
 * @file e32p_ws2812.c
 * @brief e32p_ws2812 implementation — NeoPixel driver over RMT.
 *
 * A composite RMT encoder (bytes-encoder for the GRB stream + copy-encoder for
 * the reset/latch pulse) turns a flat GRB byte buffer into the WS2812 line code.
 * The encoder design mirrors the ESP-IDF rmt/led_strip example.
 */
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_check.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"
#include "e32p_ws2812.h"

static const char *TAG = "e32p_ws2812";

/* 10 MHz RMT resolution -> 1 tick = 0.1 us. High enough for WS2812 bit timing. */
#define E32P_WS2812_RES_HZ 10000000u

/* ---- Composite WS2812 encoder ------------------------------------------- */

typedef struct {
    rmt_encoder_t     base;          /* driver-facing vtable */
    rmt_encoder_t    *bytes_encoder; /* encodes GRB data bits */
    rmt_encoder_t    *copy_encoder;  /* emits the reset code */
    int               state;         /* 0 = sending data, 1 = sending reset */
    rmt_symbol_word_t reset_code;    /* >= 50 us low latch pulse */
} e32p_ws2812_encoder_t;

static size_t ws2812_encode(rmt_encoder_t *encoder, rmt_channel_handle_t channel,
                            const void *primary_data, size_t data_size,
                            rmt_encode_state_t *ret_state)
{
    e32p_ws2812_encoder_t *enc = __containerof(encoder, e32p_ws2812_encoder_t, base);
    rmt_encoder_handle_t bytes_enc = enc->bytes_encoder;
    rmt_encoder_handle_t copy_enc  = enc->copy_encoder;
    rmt_encode_state_t session_state = RMT_ENCODING_RESET;
    rmt_encode_state_t state = RMT_ENCODING_RESET;
    size_t encoded_symbols = 0;

    switch (enc->state) {
    case 0: /* GRB payload */
        encoded_symbols += bytes_enc->encode(bytes_enc, channel, primary_data,
                                             data_size, &session_state);
        if (session_state & RMT_ENCODING_COMPLETE) {
            enc->state = 1; /* advance to the reset pulse */
        }
        if (session_state & RMT_ENCODING_MEM_FULL) {
            state |= RMT_ENCODING_MEM_FULL;
            goto out; /* yield: RMT memory is full */
        }
    /* fall through */
    case 1: /* reset / latch */
        encoded_symbols += copy_enc->encode(copy_enc, channel, &enc->reset_code,
                                            sizeof(enc->reset_code), &session_state);
        if (session_state & RMT_ENCODING_COMPLETE) {
            enc->state = 0; /* ready for the next frame */
            state |= RMT_ENCODING_COMPLETE;
        }
        if (session_state & RMT_ENCODING_MEM_FULL) {
            state |= RMT_ENCODING_MEM_FULL;
            goto out;
        }
    }
out:
    *ret_state = state;
    return encoded_symbols;
}

static esp_err_t ws2812_encoder_reset(rmt_encoder_t *encoder)
{
    e32p_ws2812_encoder_t *enc = __containerof(encoder, e32p_ws2812_encoder_t, base);
    rmt_encoder_reset(enc->bytes_encoder);
    rmt_encoder_reset(enc->copy_encoder);
    enc->state = 0;
    return ESP_OK;
}

static esp_err_t ws2812_encoder_del(rmt_encoder_t *encoder)
{
    e32p_ws2812_encoder_t *enc = __containerof(encoder, e32p_ws2812_encoder_t, base);
    rmt_del_encoder(enc->bytes_encoder);
    rmt_del_encoder(enc->copy_encoder);
    free(enc);
    return ESP_OK;
}

static esp_err_t ws2812_new_encoder(rmt_encoder_handle_t *ret_encoder)
{
    esp_err_t ret = ESP_OK;
    e32p_ws2812_encoder_t *enc = calloc(1, sizeof(*enc));
    ESP_RETURN_ON_FALSE(enc != NULL, ESP_ERR_NO_MEM, TAG, "encoder alloc failed");

    enc->base.encode = ws2812_encode;
    enc->base.reset  = ws2812_encoder_reset;
    enc->base.del    = ws2812_encoder_del;

    /* 10 MHz -> 10 ticks/us. WS2812: T0H 0.3us / T0L 0.9us, T1H 0.9us / T1L 0.3us. */
    const uint32_t ticks_per_us = E32P_WS2812_RES_HZ / 1000000u;
    rmt_bytes_encoder_config_t bytes_cfg = {
        .bit0 = { .level0 = 1, .duration0 = (uint16_t)(0.3 * ticks_per_us),
                  .level1 = 0, .duration1 = (uint16_t)(0.9 * ticks_per_us) },
        .bit1 = { .level0 = 1, .duration0 = (uint16_t)(0.9 * ticks_per_us),
                  .level1 = 0, .duration1 = (uint16_t)(0.3 * ticks_per_us) },
        .flags.msb_first = 1, /* WS2812 bit order: G7..G0 R7..R0 B7..B0 */
    };
    ESP_GOTO_ON_ERROR(rmt_new_bytes_encoder(&bytes_cfg, &enc->bytes_encoder), err,
                      TAG, "bytes encoder failed");

    rmt_copy_encoder_config_t copy_cfg = {};
    ESP_GOTO_ON_ERROR(rmt_new_copy_encoder(&copy_cfg, &enc->copy_encoder), err,
                      TAG, "copy encoder failed");

    /* Latch: hold the line low ~50 us (split across the two symbol halves). */
    const uint16_t reset_ticks = (uint16_t)(ticks_per_us * 50 / 2);
    enc->reset_code = (rmt_symbol_word_t){ .level0 = 0, .duration0 = reset_ticks,
                                           .level1 = 0, .duration1 = reset_ticks };

    *ret_encoder = &enc->base;
    return ESP_OK;

err:
    if (enc->bytes_encoder) {
        rmt_del_encoder(enc->bytes_encoder);
    }
    free(enc);
    return ret;
}

/* ---- Public facade ------------------------------------------------------ */

struct e32p_ws2812_ctx {
    rmt_channel_handle_t chan;
    rmt_encoder_handle_t encoder;
    uint8_t             *grb;      /* num_leds * 3 bytes, G,R,B order */
    int                  num_leds;
};

esp_err_t e32p_ws2812_init(int gpio, int num_leds, e32p_ws2812_handle_t *out)
{
    E32P_CHECK_ARG(out != NULL, TAG);
    E32P_CHECK_ARG(gpio >= 0, TAG);
    E32P_CHECK_ARG(num_leds >= 1, TAG);

    struct e32p_ws2812_ctx *ctx = calloc(1, sizeof(*ctx));
    ESP_RETURN_ON_FALSE(ctx != NULL, ESP_ERR_NO_MEM, TAG, "ctx alloc failed");
    ctx->num_leds = num_leds;
    ctx->grb = calloc((size_t)num_leds * 3, 1);
    if (ctx->grb == NULL) {
        free(ctx);
        return ESP_ERR_NO_MEM;
    }

    rmt_tx_channel_config_t chan_cfg = {
        .gpio_num          = gpio,
        .clk_src           = RMT_CLK_SRC_DEFAULT,
        .resolution_hz     = E32P_WS2812_RES_HZ,
        .mem_block_symbols = 64,
        .trans_queue_depth = 4,
    };
    esp_err_t err = rmt_new_tx_channel(&chan_cfg, &ctx->chan);
    if (err != ESP_OK) {
        goto fail_chan;
    }
    err = ws2812_new_encoder(&ctx->encoder);
    if (err != ESP_OK) {
        goto fail_enc;
    }
    err = rmt_enable(ctx->chan);
    if (err != ESP_OK) {
        goto fail_enable;
    }

    *out = ctx;
    ESP_LOGI(TAG, "ws2812 init: gpio=%d leds=%d", gpio, num_leds);
    return ESP_OK;

fail_enable:
    ws2812_encoder_del(ctx->encoder);
fail_enc:
    rmt_del_channel(ctx->chan);
fail_chan:
    free(ctx->grb);
    free(ctx);
    return err;
}

esp_err_t e32p_ws2812_deinit(e32p_ws2812_handle_t h)
{
    if (h == NULL) {
        return ESP_OK;
    }
    rmt_disable(h->chan);
    ws2812_encoder_del(h->encoder);
    rmt_del_channel(h->chan);
    free(h->grb);
    free(h);
    return ESP_OK;
}

esp_err_t e32p_ws2812_set_pixel(e32p_ws2812_handle_t h, int index,
                                uint8_t r, uint8_t g, uint8_t b)
{
    E32P_CHECK_ARG(h != NULL, TAG);
    E32P_CHECK_ARG(index >= 0 && index < h->num_leds, TAG);

    uint8_t *p = &h->grb[index * 3];
    p[0] = g; /* WS2812 wire order is GRB */
    p[1] = r;
    p[2] = b;
    return ESP_OK;
}

esp_err_t e32p_ws2812_clear(e32p_ws2812_handle_t h)
{
    E32P_CHECK_ARG(h != NULL, TAG);
    memset(h->grb, 0, (size_t)h->num_leds * 3);
    return ESP_OK;
}

esp_err_t e32p_ws2812_refresh(e32p_ws2812_handle_t h)
{
    E32P_CHECK_ARG(h != NULL, TAG);
    const rmt_transmit_config_t tx_cfg = { .loop_count = 0 };
    E32P_RETURN_ON_ERR(
        rmt_transmit(h->chan, h->encoder, h->grb, (size_t)h->num_leds * 3, &tx_cfg),
        TAG);
    return rmt_tx_wait_all_done(h->chan, -1 /* wait forever */);
}
