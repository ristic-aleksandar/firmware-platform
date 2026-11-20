/**
 * @file main.c
 * @brief 14_twai_loopback — transmits a CAN frame to itself (NO_ACK self-test).
 *
 * Proves e32p_twai. The TX path runs on a bare board; a full frame loopback
 * needs a wire from TX (GPIO21) to RX (GPIO22), or an external transceiver.
 * Unlike UART, TWAI does not loop back internally.
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <string.h>
#include "e32p_twai.h"

#define TX_GPIO   21
#define RX_GPIO   22

static const char *TAG = "twai";

void app_main(void)
{
    e32p_twai_config_t cfg = { .tx_gpio = TX_GPIO, .rx_gpio = RX_GPIO,
                               .bitrate_bps = 500000, .mode = E32P_TWAI_MODE_NO_ACK };
    ESP_ERROR_CHECK(e32p_twai_init(&cfg));
    ESP_ERROR_CHECK(e32p_twai_start());
    ESP_LOGI(TAG, "TWAI NO_ACK @500k tx=%d rx=%d - jumper GPIO%d->GPIO%d (or a transceiver) for RX",
             TX_GPIO, RX_GPIO, TX_GPIO, RX_GPIO);

    for (int n = 0; ; n++) {
        e32p_twai_message_t tx = { .id = 0x123, .extended = false, .self = true, .len = 4,
                                   .data = { 0xDE, 0xAD, (uint8_t)n, 0xEF } };
        const esp_err_t terr = e32p_twai_transmit(&tx, 100);

        e32p_twai_message_t rx = {0};
        const esp_err_t rerr = e32p_twai_receive(&rx, 100);

        if (terr == ESP_OK && rerr == ESP_OK) {
            const bool match = (rx.id == tx.id) && (rx.len == tx.len) &&
                               (memcmp(rx.data, tx.data, tx.len) == 0);
            ESP_LOGI(TAG, "sent id=0x%03lx -> got id=0x%03lx data=%02x%02x%02x%02x [%s]",
                     (unsigned long)tx.id, (unsigned long)rx.id,
                     (unsigned)rx.data[0], (unsigned)rx.data[1],
                     (unsigned)rx.data[2], (unsigned)rx.data[3],
                     match ? "PASS" : "FAIL");
        } else {
            ESP_LOGW(TAG, "tx=%s rx=%s  (jumper GPIO%d->GPIO%d or add a transceiver)",
                     esp_err_to_name(terr), esp_err_to_name(rerr), TX_GPIO, RX_GPIO);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
