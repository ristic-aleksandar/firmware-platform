/**
 * @file e32p_event.c
 * @brief e32p_event implementation — default event loop facade (singleton).
 */
#include "esp_log.h"
#include "e32p_event.h"

static const char *TAG = "e32p_event";
static bool s_running = false;

esp_err_t e32p_event_init(void)
{
    if (s_running) {
        return ESP_OK;
    }
    esp_err_t err = esp_event_loop_create_default();
    /* INVALID_STATE = the default loop already exists (e.g. created by Wi-Fi). */
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    s_running = true;
    ESP_LOGI(TAG, "default event loop ready");
    return ESP_OK;
}

esp_err_t e32p_event_deinit(void)
{
    if (!s_running) {
        return ESP_OK;
    }
    E32P_RETURN_ON_ERR(esp_event_loop_delete_default(), TAG);
    s_running = false;
    return ESP_OK;
}

bool e32p_event_is_running(void)
{
    return s_running;
}

esp_err_t e32p_event_handler_register(esp_event_base_t base, int32_t id,
                                      esp_event_handler_t handler, void *arg)
{
    E32P_CHECK_ARG(handler != NULL, TAG);
    return esp_event_handler_register(base, id, handler, arg);
}

esp_err_t e32p_event_handler_unregister(esp_event_base_t base, int32_t id,
                                        esp_event_handler_t handler)
{
    E32P_CHECK_ARG(handler != NULL, TAG);
    return esp_event_handler_unregister(base, id, handler);
}

esp_err_t e32p_event_post(esp_event_base_t base, int32_t id,
                          const void *data, size_t data_size, TickType_t ticks_to_wait)
{
    return esp_event_post(base, id, data, data_size, ticks_to_wait);
}
