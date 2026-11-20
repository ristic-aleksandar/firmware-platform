/**
 * @file main.c
 * @brief 02_nvs_boot_count — persists a boot counter in NVS and dispatches it
 *        through the default event loop.
 *
 * Proves the M2 substrate: e32p_nvs (the count survives resets) and e32p_event
 * (a custom event is posted and handled). Watch the count climb by one each
 * time you reset the board.
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_event.h"
#include "e32p_nvs.h"
#include "e32p_event.h"

static const char *TAG = "boot_count";

ESP_EVENT_DEFINE_BASE(APP_EVENTS);
enum { APP_EVENT_BOOTED };

static void on_app_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)base; (void)id;
    const int32_t n = data ? *(const int32_t *)data : -1;
    ESP_LOGI(TAG, "event received: boot #%ld", (long)n);
}

void app_main(void)
{
    ESP_ERROR_CHECK(e32p_nvs_init());

    int32_t count = 0;
    esp_err_t err = e32p_nvs_read_i32("storage", "boot_count", &count);
    if (e32p_nvs_is_not_found(err)) {
        count = 0;                      /* first boot: key not written yet */
    } else {
        ESP_ERROR_CHECK(err);
    }
    count++;
    ESP_ERROR_CHECK(e32p_nvs_write_i32("storage", "boot_count", count));
    ESP_LOGI(TAG, "boot count = %ld", (long)count);

    ESP_ERROR_CHECK(e32p_event_init());
    ESP_ERROR_CHECK(e32p_event_handler_register(APP_EVENTS, APP_EVENT_BOOTED, on_app_event, NULL));
    ESP_ERROR_CHECK(e32p_event_post(APP_EVENTS, APP_EVENT_BOOTED,
                                    &count, sizeof(count), portMAX_DELAY));

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
