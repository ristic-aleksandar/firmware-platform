/**
 * @file e32p_wifi.c
 * @brief e32p_wifi implementation — STA + SoftAP facade over esp_wifi.
 */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "e32p_nvs.h"
#include "e32p_event.h"
#include "e32p_wifi.h"

static const char *TAG = "e32p_wifi";

#define E32P_WIFI_CONNECTED_BIT  BIT0
#define E32P_WIFI_FAIL_BIT       BIT1
#define E32P_WIFI_MAX_RETRY      5

static bool               s_inited = false;
static EventGroupHandle_t s_events = NULL;
static esp_netif_t       *s_sta_netif = NULL;
static esp_netif_t       *s_ap_netif = NULL;
static int                s_retry = 0;
static volatile bool      s_connected = false;
static bool               s_want_connect = false;   /* only sta_connect wants an AP association */
static char               s_ip[16] = {0};

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)base; (void)data;
    if (id == WIFI_EVENT_STA_START) {
        if (s_want_connect) {                 /* don't auto-associate when the radio is
                                                 started only for e.g. ESP-NOW */
            esp_wifi_connect();
        }
    } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        s_ip[0] = '\0';
        if (!s_want_connect) {
            return;   /* radio up for ESP-NOW etc. — not a connection we manage */
        }
        if (s_retry < E32P_WIFI_MAX_RETRY) {
            s_retry++;
            ESP_LOGW(TAG, "disconnected, retry %d/%d", s_retry, E32P_WIFI_MAX_RETRY);
            esp_wifi_connect();
        } else {
            xEventGroupSetBits(s_events, E32P_WIFI_FAIL_BIT);
        }
    }
}

static void on_ip_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)base;
    if (id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&event->ip_info.ip));
        s_retry = 0;
        s_connected = true;
        xEventGroupSetBits(s_events, E32P_WIFI_CONNECTED_BIT);
    }
}

esp_err_t e32p_wifi_init(void)
{
    if (s_inited) {
        return ESP_OK;
    }
    E32P_RETURN_ON_ERR(e32p_nvs_init(), TAG);     /* Wi-Fi stores calibration in NVS */
    E32P_RETURN_ON_ERR(e32p_event_init(), TAG);   /* default event loop */
    E32P_RETURN_ON_ERR(esp_netif_init(), TAG);

    s_events = xEventGroupCreate();
    ESP_RETURN_ON_FALSE(s_events != NULL, ESP_ERR_NO_MEM, TAG, "event group alloc failed");

    const wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    E32P_RETURN_ON_ERR(esp_wifi_init(&cfg), TAG);

    E32P_RETURN_ON_ERR(e32p_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, NULL), TAG);
    E32P_RETURN_ON_ERR(e32p_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_ip_event, NULL), TAG);

    s_inited = true;
    ESP_LOGI(TAG, "wifi stack initialized");
    return ESP_OK;
}

esp_err_t e32p_wifi_deinit(void)
{
    if (!s_inited) {
        return ESP_OK;
    }
    esp_wifi_stop();
    esp_wifi_deinit();
    if (s_sta_netif != NULL) {
        esp_netif_destroy_default_wifi(s_sta_netif);
        s_sta_netif = NULL;
    }
    if (s_ap_netif != NULL) {
        esp_netif_destroy_default_wifi(s_ap_netif);
        s_ap_netif = NULL;
    }
    if (s_events != NULL) {
        vEventGroupDelete(s_events);
        s_events = NULL;
    }
    s_inited = false;
    s_connected = false;
    return ESP_OK;
}

esp_err_t e32p_wifi_sta_connect(const char *ssid, const char *password, uint32_t timeout_ms)
{
    E32P_CHECK_ARG(ssid != NULL, TAG);
    E32P_RETURN_ON_ERR(e32p_wifi_init(), TAG);

    if (s_sta_netif == NULL) {
        s_sta_netif = esp_netif_create_default_wifi_sta();
        ESP_RETURN_ON_FALSE(s_sta_netif != NULL, ESP_FAIL, TAG, "sta netif create failed");
    }

    wifi_config_t wc = {0};
    strncpy((char *)wc.sta.ssid, ssid, sizeof(wc.sta.ssid) - 1);
    if (password != NULL) {
        strncpy((char *)wc.sta.password, password, sizeof(wc.sta.password) - 1);
    }
    wc.sta.threshold.authmode = (password != NULL && password[0] != '\0')
                                    ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

    s_retry = 0;
    s_want_connect = true;
    xEventGroupClearBits(s_events, E32P_WIFI_CONNECTED_BIT | E32P_WIFI_FAIL_BIT);

    E32P_RETURN_ON_ERR(esp_wifi_set_mode(WIFI_MODE_STA), TAG);
    E32P_RETURN_ON_ERR(esp_wifi_set_config(WIFI_IF_STA, &wc), TAG);
    E32P_RETURN_ON_ERR(esp_wifi_start(), TAG);   /* STA_START -> on_wifi_event -> connect */

    const EventBits_t bits = xEventGroupWaitBits(s_events,
        E32P_WIFI_CONNECTED_BIT | E32P_WIFI_FAIL_BIT, pdFALSE, pdFALSE,
        pdMS_TO_TICKS(timeout_ms));

    if (bits & E32P_WIFI_CONNECTED_BIT) {
        return ESP_OK;
    }
    if (bits & E32P_WIFI_FAIL_BIT) {
        return ESP_FAIL;
    }
    return ESP_ERR_TIMEOUT;
}

esp_err_t e32p_wifi_sta_disconnect(void)
{
    E32P_CHECK_ARG(s_inited, TAG);
    s_want_connect = false;          /* stop auto-reconnect */
    s_connected = false;
    return esp_wifi_disconnect();
}

bool e32p_wifi_is_connected(void)
{
    return s_connected;
}

esp_err_t e32p_wifi_get_ip(char *out_ip, size_t len)
{
    E32P_CHECK_ARG(out_ip != NULL, TAG);
    E32P_CHECK_ARG(len >= 16, TAG);
    ESP_RETURN_ON_FALSE(s_connected, ESP_ERR_INVALID_STATE, TAG, "not connected");
    strncpy(out_ip, s_ip, len - 1);
    out_ip[len - 1] = '\0';
    return ESP_OK;
}

esp_err_t e32p_wifi_get_rssi(int8_t *out_rssi)
{
    E32P_CHECK_ARG(out_rssi != NULL, TAG);
    ESP_RETURN_ON_FALSE(s_connected, ESP_ERR_INVALID_STATE, TAG, "not connected");
    wifi_ap_record_t ap;
    E32P_RETURN_ON_ERR(esp_wifi_sta_get_ap_info(&ap), TAG);
    *out_rssi = ap.rssi;
    return ESP_OK;
}

esp_err_t e32p_wifi_ap_start(const char *ssid, const char *password, uint8_t channel, uint8_t max_conn)
{
    E32P_CHECK_ARG(ssid != NULL, TAG);
    E32P_CHECK_ARG(channel >= 1 && channel <= 13, TAG);
    E32P_CHECK_ARG(max_conn >= 1 && max_conn <= 10, TAG);
    E32P_RETURN_ON_ERR(e32p_wifi_init(), TAG);

    if (s_ap_netif == NULL) {
        s_ap_netif = esp_netif_create_default_wifi_ap();
        ESP_RETURN_ON_FALSE(s_ap_netif != NULL, ESP_FAIL, TAG, "ap netif create failed");
    }

    wifi_config_t wc = {0};
    strncpy((char *)wc.ap.ssid, ssid, sizeof(wc.ap.ssid) - 1);
    wc.ap.ssid_len       = strlen(ssid);
    wc.ap.channel        = channel;
    wc.ap.max_connection = max_conn;
    if (password != NULL && password[0] != '\0') {
        strncpy((char *)wc.ap.password, password, sizeof(wc.ap.password) - 1);
        wc.ap.authmode = WIFI_AUTH_WPA2_PSK;
    } else {
        wc.ap.authmode = WIFI_AUTH_OPEN;
    }

    E32P_RETURN_ON_ERR(esp_wifi_set_mode(WIFI_MODE_AP), TAG);
    E32P_RETURN_ON_ERR(esp_wifi_set_config(WIFI_IF_AP, &wc), TAG);
    E32P_RETURN_ON_ERR(esp_wifi_start(), TAG);
    ESP_LOGI(TAG, "SoftAP started: ssid=%s channel=%u", ssid, (unsigned)channel);
    return ESP_OK;
}

esp_err_t e32p_wifi_ap_stop(void)
{
    E32P_CHECK_ARG(s_inited, TAG);
    return esp_wifi_stop();
}
