/**
 * @file hil_wireless.c
 * @brief Wi-Fi / ESP-NOW HIL tests + the ESP-NOW peer companion.
 */
#include <string.h>
#include <stdio.h>
#include "e32p_wifi.h"
#include "e32p_espnow.h"
#include "hil_agent.h"
#include "hil_wireless.h"

hil_verdict_t hil_wl_wifi_neg(char *d, size_t n)
{
    if (e32p_wifi_sta_connect(NULL, "x", 100) != ESP_ERR_INVALID_ARG) {
        snprintf(d, n, "NULL ssid not rejected");
        return HIL_FAIL;
    }
    if (e32p_wifi_ap_start("ap", NULL, 99, 4) != ESP_ERR_INVALID_ARG) {
        snprintf(d, n, "bad channel not rejected");
        return HIL_FAIL;
    }
    snprintf(d, n, "wifi args rejected");
    return HIL_PASS;
}

hil_verdict_t hil_wl_espnow_neg(char *d, size_t n)
{
    uint8_t mac[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    uint8_t data[4] = { 1, 2, 3, 4 };
    if (e32p_espnow_send(mac, NULL, 4) != ESP_ERR_INVALID_ARG) {
        snprintf(d, n, "NULL data not rejected");
        return HIL_FAIL;
    }
    if (e32p_espnow_broadcast(data, 300) != ESP_ERR_INVALID_ARG) {
        snprintf(d, n, "len>250 not rejected");
        return HIL_FAIL;
    }
    if (e32p_espnow_send(mac, data, 4) != ESP_ERR_E32P_NOT_INIT) {
        snprintf(d, n, "send-before-init not rejected");
        return HIL_FAIL;
    }
    snprintf(d, n, "espnow args rejected");
    return HIL_PASS;
}

hil_verdict_t hil_wl_espnow_xfer(char *d, size_t n)
{
    if (e32p_espnow_init(NULL) != ESP_OK) {
        snprintf(d, n, "espnow init failed (needs radio)");
        return HIL_FAIL;
    }
    uint8_t data[8] = { 0xE5, 1, 2, 3, 4, 5, 6, 7 };
    esp_err_t err = e32p_espnow_broadcast(data, 8);
    e32p_espnow_deinit();
    if (err != ESP_OK) {
        snprintf(d, n, "broadcast=%s", esp_err_to_name(err));
        return HIL_FAIL;
    }
    snprintf(d, n, "broadcast 8B sent (companion should @EVT ESPNOW)");
    return HIL_PASS;
}

/* --- ESP-NOW peer companion --- */

static void espnow_recv_cb(const uint8_t src[6], const uint8_t *data, int len)
{
    (void)data;
    hil_emit("@EVT ESPNOW from=%02x:%02x:%02x:%02x:%02x:%02x len=%d",
             src[0], src[1], src[2], src[3], src[4], src[5], len);
}

void hil_wireless_start_companion(void)
{
    if (e32p_espnow_init(espnow_recv_cb) != ESP_OK) {
        hil_emit("@EVT ESPNOW_ERR init failed");
        return;
    }
    hil_emit("@EVT COMPANION espnow peer ready");
}
