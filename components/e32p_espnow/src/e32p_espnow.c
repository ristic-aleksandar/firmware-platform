/**
 * @file e32p_espnow.c
 * @brief e32p_espnow implementation — peer-to-peer messaging facade over esp_now.
 */
#include <string.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "e32p_wifi.h"
#include "e32p_espnow.h"

static const char *TAG = "e32p_espnow";

static const uint8_t BROADCAST_MAC[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

static e32p_espnow_recv_cb_t s_user_recv = NULL;
static bool s_inited = false;
static bool s_bcast_added = false;

static void on_recv_trampoline(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    if (s_user_recv != NULL && info != NULL) {
        s_user_recv(info->src_addr, data, len);   /* hide esp_now types from the user */
    }
}

static void on_send_cb(const uint8_t *mac, esp_now_send_status_t status)
{
    (void)mac;
    if (status != ESP_NOW_SEND_SUCCESS) {
        ESP_LOGW(TAG, "send reported failure");
    }
}

static esp_err_t add_peer_internal(const uint8_t mac[6])
{
    esp_now_peer_info_t peer = {0};
    memcpy(peer.peer_addr, mac, 6);
    peer.channel = 0;              /* current channel */
    peer.ifidx   = WIFI_IF_STA;
    peer.encrypt = false;
    esp_err_t err = esp_now_add_peer(&peer);
    return (err == ESP_ERR_ESPNOW_EXIST) ? ESP_OK : err;
}

esp_err_t e32p_espnow_init(e32p_espnow_recv_cb_t on_recv)
{
    if (s_inited) {
        s_user_recv = on_recv;
        return ESP_OK;
    }
    E32P_RETURN_ON_ERR(e32p_wifi_init(), TAG);                 /* netif + nvs + event + wifi_init */
    E32P_RETURN_ON_ERR(esp_wifi_set_mode(WIFI_MODE_STA), TAG);
    E32P_RETURN_ON_ERR(esp_wifi_start(), TAG);                 /* radio up, not connected */
    E32P_RETURN_ON_ERR(esp_now_init(), TAG);

    s_user_recv = on_recv;
    E32P_RETURN_ON_ERR(esp_now_register_recv_cb(on_recv_trampoline), TAG);
    E32P_RETURN_ON_ERR(esp_now_register_send_cb(on_send_cb), TAG);

    s_inited = true;
    ESP_LOGI(TAG, "esp-now ready");
    return ESP_OK;
}

esp_err_t e32p_espnow_deinit(void)
{
    if (!s_inited) {
        return ESP_OK;
    }
    esp_now_deinit();
    s_inited = false;
    s_bcast_added = false;
    s_user_recv = NULL;
    return ESP_OK;
}

esp_err_t e32p_espnow_add_peer(const uint8_t mac[6])
{
    E32P_CHECK_ARG(mac != NULL, TAG);
    E32P_RETURN_ON_FALSE(s_inited, ESP_ERR_E32P_NOT_INIT, TAG, "call e32p_espnow_init first");
    return add_peer_internal(mac);
}

esp_err_t e32p_espnow_remove_peer(const uint8_t mac[6])
{
    E32P_CHECK_ARG(mac != NULL, TAG);
    E32P_RETURN_ON_FALSE(s_inited, ESP_ERR_E32P_NOT_INIT, TAG, "call e32p_espnow_init first");
    return esp_now_del_peer(mac);
}

esp_err_t e32p_espnow_send(const uint8_t mac[6], const void *data, size_t len)
{
    E32P_CHECK_ARG(mac != NULL, TAG);
    E32P_CHECK_ARG(data != NULL, TAG);
    E32P_CHECK_ARG(len > 0 && len <= ESP_NOW_MAX_DATA_LEN, TAG);
    E32P_RETURN_ON_FALSE(s_inited, ESP_ERR_E32P_NOT_INIT, TAG, "call e32p_espnow_init first");
    return esp_now_send(mac, (const uint8_t *)data, len);
}

esp_err_t e32p_espnow_broadcast(const void *data, size_t len)
{
    E32P_CHECK_ARG(data != NULL, TAG);
    E32P_CHECK_ARG(len > 0 && len <= ESP_NOW_MAX_DATA_LEN, TAG);
    E32P_RETURN_ON_FALSE(s_inited, ESP_ERR_E32P_NOT_INIT, TAG, "call e32p_espnow_init first");

    if (!s_bcast_added) {
        E32P_RETURN_ON_ERR(add_peer_internal(BROADCAST_MAC), TAG);
        s_bcast_added = true;
    }
    return esp_now_send(BROADCAST_MAC, (const uint8_t *)data, len);
}

esp_err_t e32p_espnow_get_mac(uint8_t out_mac[6])
{
    E32P_CHECK_ARG(out_mac != NULL, TAG);
    return esp_wifi_get_mac(WIFI_IF_STA, out_mac);
}
