/**
 * @file main.c
 * @brief 12_wifi_sta — connects to a Wi-Fi network and prints IP + RSSI.
 *
 * Proves e32p_wifi (Station mode). Credentials come from menuconfig
 * (Example Wi-Fi Configuration) — never hardcoded here.
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "e32p_wifi.h"

static const char *TAG = "wifi_sta";

void app_main(void)
{
    ESP_ERROR_CHECK(e32p_wifi_init());
    ESP_LOGI(TAG, "connecting to \"%s\"...", CONFIG_E32P_EXAMPLE_WIFI_SSID);

    esp_err_t err = e32p_wifi_sta_connect(CONFIG_E32P_EXAMPLE_WIFI_SSID,
                                          CONFIG_E32P_EXAMPLE_WIFI_PASSWORD, 15000);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "connect failed: %s (set SSID/password via `idf.py menuconfig`)",
                 esp_err_to_name(err));
        return;
    }

    char ip[16] = {0};
    int8_t rssi = 0;
    e32p_wifi_get_ip(ip, sizeof(ip));
    e32p_wifi_get_rssi(&rssi);
    ESP_LOGI(TAG, "connected, IP=%s  RSSI=%d dBm", ip, rssi);

    while (1) {
        if (e32p_wifi_is_connected() && e32p_wifi_get_rssi(&rssi) == ESP_OK) {
            ESP_LOGI(TAG, "online  RSSI=%d dBm", rssi);
        } else {
            ESP_LOGW(TAG, "offline (reconnecting...)");
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
