/**
 * @file e32p_adc.c
 * @brief e32p_adc implementation — oneshot ADC + line-fitting calibration.
 */
#include <stdlib.h>
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "e32p_adc.h"

static const char *TAG = "e32p_adc";

/* ESP-IDF exposes no ADC_CHANNEL_MAX. ESP32's ADC2 has the most channels (0..9);
 * 10 is a safe upper bound for the per-channel calibration table on any target. */
#define E32P_ADC_MAX_CHANNELS  10

struct e32p_adc_ctx {
    adc_oneshot_unit_handle_t unit;
    adc_unit_t                unit_id;
    adc_cali_handle_t         cali[E32P_ADC_MAX_CHANNELS];   /* per-channel; NULL if none */
};

static adc_atten_t map_atten(e32p_adc_atten_t a)
{
    switch (a) {
    case E32P_ADC_ATTEN_0DB:   return ADC_ATTEN_DB_0;
    case E32P_ADC_ATTEN_2_5DB: return ADC_ATTEN_DB_2_5;
    case E32P_ADC_ATTEN_6DB:   return ADC_ATTEN_DB_6;
    case E32P_ADC_ATTEN_12DB:
    default:                   return ADC_ATTEN_DB_12;
    }
}

esp_err_t e32p_adc_init(int unit, e32p_adc_handle_t *out)
{
    E32P_CHECK_ARG(out != NULL, TAG);
    E32P_CHECK_ARG(unit == 1 || unit == 2, TAG);

    struct e32p_adc_ctx *ctx = calloc(1, sizeof(*ctx));
    ESP_RETURN_ON_FALSE(ctx != NULL, ESP_ERR_NO_MEM, TAG, "alloc failed");

    ctx->unit_id = (unit == 2) ? ADC_UNIT_2 : ADC_UNIT_1;
    const adc_oneshot_unit_init_cfg_t icfg = {
        .unit_id  = ctx->unit_id,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    esp_err_t err = adc_oneshot_new_unit(&icfg, &ctx->unit);
    if (err != ESP_OK) {
        free(ctx);
        return err;
    }
    *out = ctx;
    ESP_LOGI(TAG, "adc%d unit init", unit);
    return ESP_OK;
}

esp_err_t e32p_adc_deinit(e32p_adc_handle_t h)
{
    if (h == NULL) {
        return ESP_OK;
    }
    for (int i = 0; i < E32P_ADC_MAX_CHANNELS; i++) {
        if (h->cali[i] != NULL) {
            adc_cali_delete_scheme_line_fitting(h->cali[i]);
        }
    }
    adc_oneshot_del_unit(h->unit);
    free(h);
    return ESP_OK;
}

esp_err_t e32p_adc_config_channel(e32p_adc_handle_t h, int channel, e32p_adc_atten_t atten)
{
    E32P_CHECK_ARG(h != NULL, TAG);
    E32P_CHECK_ARG(channel >= 0 && channel < E32P_ADC_MAX_CHANNELS, TAG);

    const adc_atten_t a = map_atten(atten);
    const adc_oneshot_chan_cfg_t cc = {
        .atten    = a,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    E32P_RETURN_ON_ERR(adc_oneshot_config_channel(h->unit, (adc_channel_t)channel, &cc), TAG);

    /* Best-effort calibration; if it fails, read_voltage is simply unavailable. */
    if (h->cali[channel] == NULL) {
        const adc_cali_line_fitting_config_t lf = {
            .unit_id  = h->unit_id,
            .atten    = a,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        esp_err_t cerr = adc_cali_create_scheme_line_fitting(&lf, &h->cali[channel]);
        if (cerr != ESP_OK) {
            h->cali[channel] = NULL;
            ESP_LOGW(TAG, "calibration unavailable on ch%d (%s)", channel, esp_err_to_name(cerr));
        }
    }
    return ESP_OK;
}

esp_err_t e32p_adc_read_raw(e32p_adc_handle_t h, int channel, int *out_raw)
{
    E32P_CHECK_ARG(h != NULL, TAG);
    E32P_CHECK_ARG(out_raw != NULL, TAG);
    E32P_CHECK_ARG(channel >= 0 && channel < E32P_ADC_MAX_CHANNELS, TAG);

    esp_err_t err = adc_oneshot_read(h->unit, (adc_channel_t)channel, out_raw);
    if (h->unit_id == ADC_UNIT_2 && (err == ESP_ERR_TIMEOUT || err == ESP_ERR_INVALID_STATE)) {
        return ESP_ERR_E32P_RESOURCE_CONFLICT;   /* ADC2 blocked by Wi-Fi */
    }
    return err;
}

esp_err_t e32p_adc_read_voltage(e32p_adc_handle_t h, int channel, int *out_mv)
{
    E32P_CHECK_ARG(h != NULL, TAG);
    E32P_CHECK_ARG(out_mv != NULL, TAG);
    E32P_CHECK_ARG(channel >= 0 && channel < E32P_ADC_MAX_CHANNELS, TAG);
    ESP_RETURN_ON_FALSE(h->cali[channel] != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "no calibration on ch%d (config it first)", channel);

    int raw = 0;
    E32P_RETURN_ON_ERR(e32p_adc_read_raw(h, channel, &raw), TAG);
    return adc_cali_raw_to_voltage(h->cali[channel], raw, out_mv);
}
