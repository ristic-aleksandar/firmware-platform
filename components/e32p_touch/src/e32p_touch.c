/**
 * @file e32p_touch.c
 * @brief e32p_touch implementation — ESP32 capacitive touch facade.
 */
#include <stdbool.h>
#include "esp_log.h"
#include "driver/touch_sensor.h"
#include "e32p_touch.h"

static const char *TAG = "e32p_touch";
static bool s_ready = false;

esp_err_t e32p_touch_init(void)
{
    if (s_ready) {
        return ESP_OK;
    }
    E32P_RETURN_ON_ERR(touch_pad_init(), TAG);
    s_ready = true;
    ESP_LOGI(TAG, "touch subsystem init");
    return ESP_OK;
}

esp_err_t e32p_touch_deinit(void)
{
    if (!s_ready) {
        return ESP_OK;
    }
    E32P_RETURN_ON_ERR(touch_pad_deinit(), TAG);
    s_ready = false;
    return ESP_OK;
}

esp_err_t e32p_touch_config_pad(int pad)
{
    E32P_RETURN_ON_FALSE(s_ready, ESP_ERR_E32P_NOT_INIT, TAG, "call e32p_touch_init first");
    E32P_CHECK_ARG(pad >= 0 && pad < TOUCH_PAD_MAX, TAG);
    /* threshold 0: we poll touch_pad_read rather than use the interrupt FSM. */
    return touch_pad_config((touch_pad_t)pad, 0);
}

esp_err_t e32p_touch_read(int pad, uint32_t *out_value)
{
    E32P_RETURN_ON_FALSE(s_ready, ESP_ERR_E32P_NOT_INIT, TAG, "call e32p_touch_init first");
    E32P_CHECK_ARG(pad >= 0 && pad < TOUCH_PAD_MAX, TAG);
    E32P_CHECK_ARG(out_value != NULL, TAG);

    uint16_t v = 0;
    esp_err_t err = touch_pad_read((touch_pad_t)pad, &v);
    if (err == ESP_OK) {
        *out_value = v;
    }
    return err;
}
