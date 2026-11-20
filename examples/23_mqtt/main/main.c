/**
 * @file main.c
 * @brief 23_mqtt — connect to Wi-Fi, then talk to an MQTT broker.
 *
 * On connect it subscribes to a command topic and publishes a retained "online"
 * status, then heartbeats every 5 s. Inbound messages on the command topic are
 * printed. Publish anything to `e32p/demo/cmd` from another MQTT client (e.g.
 * `mosquitto_pub -h test.mosquitto.org -t e32p/demo/cmd -m hello`) and watch it
 * appear in the serial log.
 *
 * Set Wi-Fi credentials and the broker with `idf.py menuconfig` -> "23_mqtt
 * example" (they are not hardcoded).
 */
#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "e32p_wifi.h"
#include "e32p_mqtt.h"

static const char *TAG = "mqtt_demo";

#define TOPIC_STATUS "e32p/demo/status"
#define TOPIC_CMD    "e32p/demo/cmd"

static e32p_mqtt_handle_t s_mqtt;   /* set before the client connects */

static void on_mqtt(const e32p_mqtt_event_t *ev, void *user)
{
    (void)user;
    switch (ev->type) {
    case E32P_MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "broker connected; subscribing to %s", TOPIC_CMD);
        e32p_mqtt_subscribe(s_mqtt, TOPIC_CMD, 1, NULL);
        e32p_mqtt_publish(s_mqtt, TOPIC_STATUS, "online", 0, 1, true, NULL);
        break;
    case E32P_MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "RX  %.*s = %.*s", ev->topic_len, ev->topic, ev->data_len, ev->data);
        break;
    case E32P_MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "broker disconnected");
        break;
    default:
        break;
    }
}

void app_main(void)
{
    if (strlen(CONFIG_EXAMPLE_WIFI_SSID) == 0) {
        ESP_LOGE(TAG, "no Wi-Fi SSID set — run `idf.py menuconfig` -> \"23_mqtt example\"");
        return;
    }

    ESP_ERROR_CHECK(e32p_wifi_init());
    ESP_LOGI(TAG, "connecting to Wi-Fi \"%s\"...", CONFIG_EXAMPLE_WIFI_SSID);
    esp_err_t err = e32p_wifi_sta_connect(CONFIG_EXAMPLE_WIFI_SSID, CONFIG_EXAMPLE_WIFI_PASSWORD, 20000);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi connect failed: %s", esp_err_to_name(err));
        return;
    }
    char ip[16] = { 0 };
    e32p_wifi_get_ip(ip, sizeof(ip));
    ESP_LOGI(TAG, "Wi-Fi up (IP %s); connecting to broker %s", ip, CONFIG_EXAMPLE_BROKER_URI);

    e32p_mqtt_config_t cfg = { .broker_uri = CONFIG_EXAMPLE_BROKER_URI, .cb = on_mqtt };
    ESP_ERROR_CHECK(e32p_mqtt_init(&cfg, &s_mqtt));
    ESP_ERROR_CHECK(e32p_mqtt_start(s_mqtt));

    int heartbeat = 0;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        if (e32p_mqtt_is_connected(s_mqtt)) {
            char msg[32];
            snprintf(msg, sizeof(msg), "heartbeat %d", ++heartbeat);
            e32p_mqtt_publish(s_mqtt, TOPIC_STATUS, msg, 0, 0, false, NULL);
            ESP_LOGI(TAG, "TX  %s = %s", TOPIC_STATUS, msg);
        }
    }
}
