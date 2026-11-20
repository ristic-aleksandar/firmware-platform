/**
 * @file e32p_ledc.c
 * @brief e32p_ledc implementation — opaque-handle PWM facade over driver/ledc.h.
 */
#include <stdlib.h>
#include "esp_log.h"
#include "driver/ledc.h"
#include "e32p_ledc.h"

static const char *TAG = "e32p_ledc";

/* The LEDC fade service is a single global ISR. Track it so we install it once
 * and don't provoke the driver's "fade function already installed" ERROR log on
 * every additional channel. */
static bool s_fade_installed = false;

struct e32p_ledc_ctx {
    e32p_ledc_config_t cfg;
    ledc_mode_t        mode;      /* resolved speed mode */
    uint32_t           max_duty;  /* 2^bits - 1 */
    uint32_t           duty;      /* last requested duty */
    bool               running;
};

static uint32_t clamp_u32(uint32_t v, uint32_t max) { return v > max ? max : v; }

esp_err_t e32p_ledc_init(const e32p_ledc_config_t *cfg, e32p_ledc_handle_t *out)
{
    E32P_CHECK_ARG(cfg != NULL, TAG);
    E32P_CHECK_ARG(out != NULL, TAG);
    E32P_CHECK_ARG(GPIO_IS_VALID_OUTPUT_GPIO(cfg->pin), TAG);
    E32P_CHECK_ARG(cfg->freq_hz > 0, TAG);
    E32P_CHECK_ARG(cfg->duty_resolution_bits >= 1 &&
                   cfg->duty_resolution_bits <= SOC_LEDC_TIMER_BIT_WIDTH, TAG);
    E32P_CHECK_ARG(cfg->channel < SOC_LEDC_CHANNEL_NUM, TAG);
    E32P_CHECK_ARG(cfg->timer < LEDC_TIMER_MAX, TAG);

#if SOC_LEDC_SUPPORT_HS_MODE
    ledc_mode_t mode = cfg->high_speed ? LEDC_HIGH_SPEED_MODE : LEDC_LOW_SPEED_MODE;
#else
    ledc_mode_t mode = LEDC_LOW_SPEED_MODE;
    if (cfg->high_speed) {
        ESP_LOGW(TAG, "high-speed mode unsupported on this target; using low-speed");
    }
#endif

    ledc_timer_config_t tcfg = {
        .speed_mode      = mode,
        .duty_resolution = (ledc_timer_bit_t)cfg->duty_resolution_bits,
        .timer_num       = (ledc_timer_t)cfg->timer,
        .freq_hz         = cfg->freq_hz,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    E32P_RETURN_ON_ERR(ledc_timer_config(&tcfg), TAG);

    const uint32_t max_duty = (1u << cfg->duty_resolution_bits) - 1u;
    ledc_channel_config_t ccfg = {
        .gpio_num   = cfg->pin,
        .speed_mode = mode,
        .channel    = (ledc_channel_t)cfg->channel,
        .timer_sel  = (ledc_timer_t)cfg->timer,
        .duty       = clamp_u32(cfg->initial_duty, max_duty),
        .hpoint     = 0,
        .intr_type  = LEDC_INTR_DISABLE,
    };
    E32P_RETURN_ON_ERR(ledc_channel_config(&ccfg), TAG);

    /* Idle the output so a freshly-init'd handle is not driving (start() begins). */
    E32P_RETURN_ON_ERR(ledc_stop(mode, (ledc_channel_t)cfg->channel, 0), TAG);

    /* Install the global fade service once. INVALID_STATE = already installed
       (e.g. by other code), which we also treat as done. */
    if (!s_fade_installed) {
        esp_err_t ferr = ledc_fade_func_install(0);
        if (ferr != ESP_OK && ferr != ESP_ERR_INVALID_STATE) {
            return ferr;
        }
        s_fade_installed = true;
    }

    struct e32p_ledc_ctx *ctx = calloc(1, sizeof(*ctx));
    ESP_RETURN_ON_FALSE(ctx != NULL, ESP_ERR_NO_MEM, TAG, "alloc failed");
    ctx->cfg      = *cfg;
    ctx->mode     = mode;
    ctx->max_duty = max_duty;
    ctx->duty     = clamp_u32(cfg->initial_duty, max_duty);
    ctx->running  = false;

    *out = ctx;
    ESP_LOGI(TAG, "init pin=%d ch=%u freq=%luHz res=%ubit", cfg->pin,
             (unsigned)cfg->channel, (unsigned long)cfg->freq_hz,
             (unsigned)cfg->duty_resolution_bits);
    return ESP_OK;
}

esp_err_t e32p_ledc_deinit(e32p_ledc_handle_t h)
{
    if (h == NULL) {
        return ESP_OK;
    }
    ledc_stop(h->mode, (ledc_channel_t)h->cfg.channel, 0);
    free(h);
    return ESP_OK;
}

esp_err_t e32p_ledc_start(e32p_ledc_handle_t h)
{
    E32P_CHECK_ARG(h != NULL, TAG);
    E32P_RETURN_ON_ERR(ledc_set_duty(h->mode, (ledc_channel_t)h->cfg.channel, h->duty), TAG);
    E32P_RETURN_ON_ERR(ledc_update_duty(h->mode, (ledc_channel_t)h->cfg.channel), TAG);
    h->running = true;
    return ESP_OK;
}

esp_err_t e32p_ledc_stop(e32p_ledc_handle_t h)
{
    E32P_CHECK_ARG(h != NULL, TAG);
    E32P_RETURN_ON_ERR(ledc_stop(h->mode, (ledc_channel_t)h->cfg.channel, 0), TAG);
    h->running = false;
    return ESP_OK;
}

esp_err_t e32p_ledc_set_duty(e32p_ledc_handle_t h, uint32_t duty)
{
    E32P_CHECK_ARG(h != NULL, TAG);
    h->duty = clamp_u32(duty, h->max_duty);
    if (h->running) {
        E32P_RETURN_ON_ERR(ledc_set_duty(h->mode, (ledc_channel_t)h->cfg.channel, h->duty), TAG);
        E32P_RETURN_ON_ERR(ledc_update_duty(h->mode, (ledc_channel_t)h->cfg.channel), TAG);
    }
    return ESP_OK;
}

esp_err_t e32p_ledc_set_duty_percent(e32p_ledc_handle_t h, uint8_t percent)
{
    E32P_CHECK_ARG(h != NULL, TAG);
    if (percent > 100) {
        percent = 100;
    }
    return e32p_ledc_set_duty(h, (uint32_t)((uint64_t)percent * h->max_duty / 100u));
}

esp_err_t e32p_ledc_fade(e32p_ledc_handle_t h, uint32_t target_duty, uint32_t ms)
{
    E32P_CHECK_ARG(h != NULL, TAG);
    const uint32_t target = clamp_u32(target_duty, h->max_duty);
    E32P_RETURN_ON_ERR(
        ledc_set_fade_with_time(h->mode, (ledc_channel_t)h->cfg.channel, target, ms), TAG);
    E32P_RETURN_ON_ERR(
        ledc_fade_start(h->mode, (ledc_channel_t)h->cfg.channel, LEDC_FADE_NO_WAIT), TAG);
    h->duty    = target;
    h->running = true;
    return ESP_OK;
}

esp_err_t e32p_ledc_get_status(e32p_ledc_handle_t h, e32p_ledc_status_t *out)
{
    E32P_CHECK_ARG(h != NULL, TAG);
    E32P_CHECK_ARG(out != NULL, TAG);
    out->running  = h->running;
    out->duty     = h->duty;
    out->max_duty = h->max_duty;
    out->freq_hz  = h->cfg.freq_hz;
    return ESP_OK;
}
