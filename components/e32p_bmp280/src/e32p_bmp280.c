/**
 * @file e32p_bmp280.c
 * @brief e32p_bmp280 implementation — pure Bosch compensation + thin I2C driver.
 *
 * Compensation math is transcribed from the BMP280 datasheet (rev 1.1, §3.11.3);
 * it is identical to the BME280 temperature/pressure path. Kept pure so the unit
 * test can drive it with the datasheet reference vector, no bus involved.
 */
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "e32p_bmp280.h"

static const char *TAG = "e32p_bmp280";

/* Register map (datasheet §4.2). */
#define REG_ID         0xD0
#define REG_RESET      0xE0
#define REG_CONFIG     0xF5
#define REG_CTRL_MEAS  0xF4
#define REG_CALIB      0x88  /* 24 bytes: dig_T1..dig_T3, dig_P1..dig_P9 */
#define REG_DATA       0xF7  /* 6 bytes: press[msb,lsb,xlsb], temp[msb,lsb,xlsb] */
#define RESET_MAGIC    0xB6

struct e32p_bmp280_ctx {
    e32p_i2c_dev_handle_t dev;
    e32p_bmp280_calib_t   calib;
};

/* ---- Pure functions ------------------------------------------------------- */

void e32p_bmp280_parse_calib(const uint8_t r[24], e32p_bmp280_calib_t *out)
{
    if (r == NULL || out == NULL) {
        return;
    }
    /* All little-endian 16-bit words; T1 and P1 are unsigned, the rest signed. */
    out->dig_t1 = (uint16_t)(r[0]  | (r[1]  << 8));
    out->dig_t2 = (int16_t) (r[2]  | (r[3]  << 8));
    out->dig_t3 = (int16_t) (r[4]  | (r[5]  << 8));
    out->dig_p1 = (uint16_t)(r[6]  | (r[7]  << 8));
    out->dig_p2 = (int16_t) (r[8]  | (r[9]  << 8));
    out->dig_p3 = (int16_t) (r[10] | (r[11] << 8));
    out->dig_p4 = (int16_t) (r[12] | (r[13] << 8));
    out->dig_p5 = (int16_t) (r[14] | (r[15] << 8));
    out->dig_p6 = (int16_t) (r[16] | (r[17] << 8));
    out->dig_p7 = (int16_t) (r[18] | (r[19] << 8));
    out->dig_p8 = (int16_t) (r[20] | (r[21] << 8));
    out->dig_p9 = (int16_t) (r[22] | (r[23] << 8));
}

int32_t e32p_bmp280_compensate_temp(int32_t adc_t, const e32p_bmp280_calib_t *c, int32_t *t_fine)
{
    int32_t var1, var2, fine;
    var1 = ((((adc_t >> 3) - ((int32_t)c->dig_t1 << 1))) * ((int32_t)c->dig_t2)) >> 11;
    var2 = (((((adc_t >> 4) - ((int32_t)c->dig_t1)) *
              (((adc_t >> 4) - ((int32_t)c->dig_t1)))) >> 12) * ((int32_t)c->dig_t3)) >> 14;
    fine = var1 + var2;
    if (t_fine != NULL) {
        *t_fine = fine;
    }
    return (fine * 5 + 128) >> 8;   /* 0.01 °C */
}

uint32_t e32p_bmp280_compensate_press(int32_t adc_p, const e32p_bmp280_calib_t *c, int32_t t_fine)
{
    int64_t var1, var2, p;
    var1 = ((int64_t)t_fine) - 128000;
    var2 = var1 * var1 * (int64_t)c->dig_p6;
    var2 = var2 + ((var1 * (int64_t)c->dig_p5) << 17);
    var2 = var2 + (((int64_t)c->dig_p4) << 35);
    var1 = ((var1 * var1 * (int64_t)c->dig_p3) >> 8) + ((var1 * (int64_t)c->dig_p2) << 12);
    var1 = (((((int64_t)1) << 47) + var1)) * ((int64_t)c->dig_p1) >> 33;
    if (var1 == 0) {
        return 0;   /* avoid division by zero */
    }
    p = 1048576 - adc_p;
    p = (((p << 31) - var2) * 3125) / var1;
    var1 = (((int64_t)c->dig_p9) * (p >> 13) * (p >> 13)) >> 25;
    var2 = (((int64_t)c->dig_p8) * p) >> 19;
    p = ((p + var1 + var2) >> 8) + (((int64_t)c->dig_p7) << 4);
    return (uint32_t)(p >> 8);   /* Q24.8 -> Pa */
}

/* ---- Device functions ----------------------------------------------------- */

esp_err_t e32p_bmp280_init(const e32p_bmp280_config_t *cfg, e32p_bmp280_handle_t *out)
{
    E32P_CHECK_ARG(cfg != NULL, TAG);
    E32P_CHECK_ARG(out != NULL, TAG);
    E32P_CHECK_ARG(cfg->bus != NULL, TAG);
    E32P_CHECK_ARG(cfg->addr == E32P_BMP280_ADDR_PRIMARY ||
                   cfg->addr == E32P_BMP280_ADDR_SECONDARY, TAG);

    struct e32p_bmp280_ctx *ctx = calloc(1, sizeof(*ctx));
    if (ctx == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = e32p_i2c_add_device(cfg->bus, cfg->addr, cfg->scl_speed_hz, &ctx->dev);
    if (err != ESP_OK) {
        free(ctx);
        return err;
    }

    uint8_t id = 0;
    err = e32p_i2c_read_reg(ctx->dev, REG_ID, &id, 1, 100);
    if (err != ESP_OK) {
        goto fail;
    }
    if (id != E32P_BMP280_CHIP_ID) {
        ESP_LOGE(TAG, "chip id 0x%02x at 0x%02x is not BMP280 (0x58)", id, cfg->addr);
        err = ESP_ERR_NOT_FOUND;
        goto fail;
    }

    /* Soft reset, then let the NVM copy settle before reading calibration. */
    err = e32p_i2c_write_reg(ctx->dev, REG_RESET, RESET_MAGIC, 100);
    if (err != ESP_OK) {
        goto fail;
    }
    /* After reset the BMP280 copies its factory trim from NVM (t_startup ~2 ms).
       Read it; if the trim still comes back all-zero (a slow part), wait and retry
       before giving up — zero trim would silently make the compensation output 0. */
    uint8_t raw[24];
    for (int attempt = 0; attempt < 5; attempt++) {
        vTaskDelay(pdMS_TO_TICKS(10));
        err = e32p_i2c_read_reg(ctx->dev, REG_CALIB, raw, sizeof(raw), 100);
        if (err != ESP_OK) {
            goto fail;
        }
        e32p_bmp280_parse_calib(raw, &ctx->calib);
        if (ctx->calib.dig_t1 != 0 || ctx->calib.dig_p1 != 0) {
            break;
        }
    }
    if (ctx->calib.dig_t1 == 0 && ctx->calib.dig_p1 == 0) {
        ESP_LOGE(TAG, "calibration reads all-zero at 0x%02x - blank NVM "
                 "(counterfeit/defective sensor?)", cfg->addr);
        err = ESP_ERR_INVALID_RESPONSE;
        goto fail;
    }

    /* Write config in sleep (post-reset) before ctrl_meas moves us to normal. */
    const uint8_t config = (uint8_t)((cfg->standby << 5) | (cfg->filter << 2));
    err = e32p_i2c_write_reg(ctx->dev, REG_CONFIG, config, 100);
    if (err != ESP_OK) {
        goto fail;
    }
    const uint8_t ctrl = (uint8_t)((cfg->osr_temp << 5) | (cfg->osr_press << 2) | cfg->mode);
    err = e32p_i2c_write_reg(ctx->dev, REG_CTRL_MEAS, ctrl, 100);
    if (err != ESP_OK) {
        goto fail;
    }

    ESP_LOGI(TAG, "bmp280 init at 0x%02x: T1=%u P1=%u ctrl=0x%02x cfg=0x%02x",
             cfg->addr, ctx->calib.dig_t1, ctx->calib.dig_p1, ctrl, config);
    *out = ctx;
    return ESP_OK;

fail:
    e32p_i2c_remove_device(ctx->dev);
    free(ctx);
    return err;
}

esp_err_t e32p_bmp280_deinit(e32p_bmp280_handle_t h)
{
    if (h == NULL) {
        return ESP_OK;
    }
    e32p_i2c_remove_device(h->dev);
    free(h);
    return ESP_OK;
}

esp_err_t e32p_bmp280_read_raw(e32p_bmp280_handle_t h, int32_t *adc_t, int32_t *adc_p)
{
    E32P_CHECK_ARG(h != NULL, TAG);
    uint8_t d[6];
    E32P_RETURN_ON_ERR(e32p_i2c_read_reg(h->dev, REG_DATA, d, sizeof(d), 100), TAG);
    if (adc_p != NULL) {
        *adc_p = (int32_t)(((uint32_t)d[0] << 12) | ((uint32_t)d[1] << 4) | (d[2] >> 4));
    }
    if (adc_t != NULL) {
        *adc_t = (int32_t)(((uint32_t)d[3] << 12) | ((uint32_t)d[4] << 4) | (d[5] >> 4));
    }
    return ESP_OK;
}

esp_err_t e32p_bmp280_read(e32p_bmp280_handle_t h, e32p_bmp280_reading_t *out)
{
    E32P_CHECK_ARG(h != NULL, TAG);
    E32P_CHECK_ARG(out != NULL, TAG);

    int32_t adc_t, adc_p, t_fine;
    E32P_RETURN_ON_ERR(e32p_bmp280_read_raw(h, &adc_t, &adc_p), TAG);
    out->temperature_c100 = e32p_bmp280_compensate_temp(adc_t, &h->calib, &t_fine);
    out->pressure_pa      = e32p_bmp280_compensate_press(adc_p, &h->calib, t_fine);
    return ESP_OK;
}

const e32p_bmp280_calib_t *e32p_bmp280_calib(e32p_bmp280_handle_t h)
{
    return (h != NULL) ? &h->calib : NULL;
}
