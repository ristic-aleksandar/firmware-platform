/**
 * @file main.c
 * @brief 27_ble — a BLE GATT peripheral you can poke from your phone.
 *
 * Advertises as "e32p-ble" with one service (0x00FF) and two characteristics:
 *   0x2A56  status   read + notify  — a counter that ticks every 2 s
 *   0x2A57  command  write          — whatever you write is printed to the log
 *
 * Connect with nRF Connect / LightBlue (or any BLE central): subscribe to 0x2A56
 * to watch the counter arrive as notifications, and write text to 0x2A57 to see
 * it appear in the serial monitor. No wiring — BLE is on-chip.
 */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "e32p_nvs.h"
#include "e32p_ble.h"

static const char *TAG = "ble_demo";

enum { CH_STATUS = 0, CH_CMD = 1 };

static void on_write(uint16_t idx, const uint8_t *data, uint16_t len, void *user)
{
    (void)user;
    ESP_LOGI(TAG, "write to char %u: \"%.*s\" (%u bytes)", idx, len, (const char *)data, len);
}

static void on_conn(bool connected, void *user)
{
    (void)user;
    ESP_LOGI(TAG, "central %s", connected ? "connected" : "disconnected");
}

void app_main(void)
{
    /* Local (not static): the UUID helpers are function calls, so they can't
       appear in a static initializer. init() reads this during the call. */
    const e32p_ble_char_t chars[] = {
        { .uuid = e32p_ble_uuid16(0x2A56), .props = E32P_BLE_PROP_READ | E32P_BLE_PROP_NOTIFY },
        { .uuid = e32p_ble_uuid16(0x2A57), .props = E32P_BLE_PROP_WRITE },
    };
    e32p_ble_config_t cfg = {
        .device_name = "e32p-ble",
        .service_uuid = e32p_ble_uuid16(0x00FF),
        .chars = chars, .n_chars = 2,
        .on_write = on_write, .on_conn = on_conn,
    };
    ESP_ERROR_CHECK(e32p_nvs_init());   /* BLE stack stores PHY cal + bond keys here */
    ESP_ERROR_CHECK(e32p_ble_init(&cfg));
    ESP_LOGI(TAG, "advertising as 'e32p-ble' — connect with nRF Connect / LightBlue");
    ESP_LOGI(TAG, "service 0x00FF: 0x2A56 read/notify status, 0x2A57 write command");

    uint8_t counter = 0;
    e32p_ble_set_value(CH_STATUS, &counter, 1);   /* initial readable value */

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(2000));
        counter++;
        if (e32p_ble_is_connected()) {
            if (e32p_ble_notify(CH_STATUS, &counter, 1) == ESP_OK) {
                ESP_LOGI(TAG, "notified status=%u", counter);
            }
        } else {
            e32p_ble_set_value(CH_STATUS, &counter, 1);   /* keep a read current */
        }
    }
}
