/**
 * @file hil_net.c
 * @brief Network HIL commands — the automated counterparts of the manual MQTT
 *        (Mosquitto) and HTTP-OTA (python http.server) bench procedures. The host
 *        plays broker / file server; these handlers drive e32p_wifi / e32p_mqtt /
 *        e32p_ota and report over the control UART.
 */
#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "e32p_wifi.h"
#include "e32p_mqtt.h"
#include "e32p_ota.h"
#include "hil_agent.h"
#include "hil_net.h"

#define MQTT_TOPIC_ECHO "hil/mqtt/echo"   /* device -> broker -> host */
#define MQTT_TOPIC_CMD  "hil/mqtt/cmd"    /* host -> broker -> device */

void hil_net_wifi(const char *ssid, const char *pass)
{
    /* Idempotent: a later test may call WIFI again while we're still up. Don't
       tear down a working connection (reconnecting can time out). */
    if (e32p_wifi_is_connected()) {
        char cur[16] = { 0 };
        e32p_wifi_get_ip(cur, sizeof(cur));
        hil_emit("@RSP WIFI OK ip=%s already", cur);
        return;
    }
    if (e32p_wifi_init() != ESP_OK) {
        hil_emit("@RSP WIFI FAIL init");
        return;
    }
    esp_err_t err = e32p_wifi_sta_connect(ssid, (pass && pass[0]) ? pass : NULL, 20000);
    if (err != ESP_OK) {
        hil_emit("@RSP WIFI FAIL %s", esp_err_to_name(err));
        return;
    }
    char ip[16] = { 0 };
    e32p_wifi_get_ip(ip, sizeof(ip));
    hil_emit("@RSP WIFI OK ip=%s", ip);
}

/* Captures the payload the host publishes to MQTT_TOPIC_CMD. */
static volatile bool s_mqtt_got;
static char s_mqtt_rx[64];

static void mqtt_cb(const e32p_mqtt_event_t *ev, void *user)
{
    (void)user;
    if (ev->type == E32P_MQTT_EVENT_DATA) {
        int n = ev->data_len;
        if (n > (int)sizeof(s_mqtt_rx) - 1) {
            n = (int)sizeof(s_mqtt_rx) - 1;
        }
        if (n > 0) {
            memcpy(s_mqtt_rx, ev->data, n);
        }
        s_mqtt_rx[n > 0 ? n : 0] = '\0';
        s_mqtt_got = true;
    }
}

void hil_net_mqtt(const char *broker_ip)
{
    char uri[64];
    snprintf(uri, sizeof(uri), "mqtt://%s:1883", broker_ip);

    s_mqtt_got = false;
    s_mqtt_rx[0] = '\0';

    e32p_mqtt_config_t cfg = { .broker_uri = uri, .cb = mqtt_cb };
    e32p_mqtt_handle_t h = NULL;
    if (e32p_mqtt_init(&cfg, &h) != ESP_OK) {
        hil_emit("@RSP MQTT FAIL init");
        return;
    }
    if (e32p_mqtt_start(h) != ESP_OK) {
        e32p_mqtt_deinit(h);
        hil_emit("@RSP MQTT FAIL start");
        return;
    }

    int waited = 0;
    while (!e32p_mqtt_is_connected(h) && waited < 10000) {
        vTaskDelay(pdMS_TO_TICKS(100));
        waited += 100;
    }
    if (!e32p_mqtt_is_connected(h)) {
        e32p_mqtt_deinit(h);
        hil_emit("@RSP MQTT FAIL noconnect");
        return;
    }

    /* Subscribe first, then announce ourselves so the host knows to send a cmd. */
    e32p_mqtt_subscribe(h, MQTT_TOPIC_CMD, 1, NULL);
    vTaskDelay(pdMS_TO_TICKS(300));
    e32p_mqtt_publish(h, MQTT_TOPIC_ECHO, "device-hello", 0, 1, false, NULL);

    waited = 0;
    while (!s_mqtt_got && waited < 8000) {
        vTaskDelay(pdMS_TO_TICKS(100));
        waited += 100;
    }
    bool got = s_mqtt_got;
    e32p_mqtt_deinit(h);

    if (got) {
        hil_emit("@RSP MQTT OK rx=%s", s_mqtt_rx);
    } else {
        hil_emit("@RSP MQTT FAIL no-rx");
    }
}

void hil_net_ota(const char *url)
{
    /* No Wi-Fi (no WIFI command since boot, or the link dropped): refuse before the
       pull. Without a working netif the https OTA call can reset the board. */
    if (!e32p_wifi_is_connected()) {
        hil_emit("@RSP OTA FAIL %s no-wifi", esp_err_to_name(ESP_ERR_INVALID_STATE));
        return;
    }
    esp_err_t err = e32p_ota_update_from_url(url, 30000);
    /* The pull (TLS handshake included) runs on this task, the main task. Report
       its stack high-water mark, success or not. On ESP-IDF the value is in
       bytes (StackType_t is uint8_t). */
    hil_emit("@EVT STACK main hwm=%u", (unsigned)uxTaskGetStackHighWaterMark(NULL));
    if (err != ESP_OK) {
        hil_emit("@RSP OTA FAIL %s", esp_err_to_name(err));
        return;
    }
    hil_emit("@RSP OTA STAGED reboot from=%s to=%s",
             e32p_ota_running_label(), e32p_ota_next_label());
    vTaskDelay(pdMS_TO_TICKS(300));   /* flush UART before the reset */
    esp_restart();
}

void hil_net_running_slot(void)
{
    const char *r = e32p_ota_running_label();
    hil_emit("@RSP OTASLOT %s", r ? r : "?");
}
