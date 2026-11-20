/**
 * @file main.c
 * @brief 13_espnow_broadcast — broadcasts a message every 2 s and logs any
 *        received ones. Proves e32p_espnow.
 *
 * One board: you see the TX path ("broadcast ... [sent]"); ESP-NOW does not
 * loop back, so no RX. Flash a SECOND board and each logs the other's messages.
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>
#include "e32p_espnow.h"

static const char *TAG = "espnow";

static void fmt_mac(char *out, const uint8_t m[6])
{
    snprintf(out, 18, "%02x:%02x:%02x:%02x:%02x:%02x",
             (unsigned)m[0], (unsigned)m[1], (unsigned)m[2],
             (unsigned)m[3], (unsigned)m[4], (unsigned)m[5]);
}

static void on_recv(const uint8_t src[6], const uint8_t *data, int len)
{
    char mac[18];
    char buf[64];
    const int n = (len < (int)sizeof(buf) - 1) ? len : (int)sizeof(buf) - 1;
    memcpy(buf, data, n);
    buf[n] = '\0';
    fmt_mac(mac, src);
    ESP_LOGI(TAG, "RX from %s : \"%s\"", mac, buf);
}

void app_main(void)
{
    ESP_ERROR_CHECK(e32p_espnow_init(on_recv));

    uint8_t mac[6];
    char macstr[18];
    e32p_espnow_get_mac(mac);
    fmt_mac(macstr, mac);
    ESP_LOGI(TAG, "this board MAC = %s - broadcasting (flash a 2nd board to see RX)", macstr);

    char msg[32];
    for (int n = 0; ; n++) {
        const int len = snprintf(msg, sizeof(msg), "e32p hello #%d", n);
        const esp_err_t err = e32p_espnow_broadcast(msg, len);
        ESP_LOGI(TAG, "broadcast \"%s\" [%s]", msg, (err == ESP_OK) ? "sent" : esp_err_to_name(err));
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
