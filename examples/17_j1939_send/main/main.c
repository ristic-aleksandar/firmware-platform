/**
 * @file main.c
 * @brief 17_j1939_send — proves e32p_j1939 on a bare board (transmit path).
 *
 * Brings up TWAI in NO_ACK self-test mode, binds a J1939 node at SA 0x80, then
 * each second broadcasts a PDU2 message (PGN 65262 / 0xFEEE, rolling counter)
 * and logs the composed 29-bit ID it put on the wire — the bare-board proof of
 * the codec + TX path (no transceiver needed).
 *
 * Receiving the frame back is a *physical* loopback: even in NO_ACK self-test
 * mode the bit leaves TX and must return on RX, so a TX->RX jumper (GPIO21->22)
 * or a transceiver is required. With that jumper in place this demo also logs
 * the decoded frame it receives. On a real bus, use E32P_TWAI_MODE_NORMAL and
 * self_reception=false.
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "e32p_twai.h"
#include "e32p_j1939.h"

static const char *TAG = "j1939_send";

#define TX_GPIO 21
#define RX_GPIO 22
#define NODE_SA 0x80u
#define DEMO_PGN 65262u   /* 0xFEEE, PDU2 broadcast */

void app_main(void)
{
    e32p_twai_config_t bus = { .tx_gpio = TX_GPIO, .rx_gpio = RX_GPIO,
                               .bitrate_bps = 250000, .mode = E32P_TWAI_MODE_NO_ACK };
    ESP_ERROR_CHECK(e32p_twai_init(&bus));
    ESP_ERROR_CHECK(e32p_twai_start());

    e32p_j1939_config_t node = { .src_addr = NODE_SA, .default_priority = 6u,
                                 .self_reception = true };
    ESP_ERROR_CHECK(e32p_j1939_init(&node));
    ESP_LOGI(TAG, "J1939 node SA=0x%02X, broadcasting PGN %u every 1s "
                  "(add a GPIO%d->%d jumper to also receive it back)",
             NODE_SA, DEMO_PGN, TX_GPIO, RX_GPIO);

    bool warned_no_rx = false;
    uint8_t counter = 0;
    for (;;) {
        e32p_j1939_message_t tx = { .priority = 0xFFu, .pgn = DEMO_PGN,
                                    .dst_addr = E32P_J1939_ADDR_GLOBAL, .len = 8,
                                    .data = { counter, 0x11, 0x22, 0x33,
                                              0x44, 0x55, 0x66, 0x77 } };
        esp_err_t err = e32p_j1939_send(&tx, 100);
        if (err == ESP_OK) {
            /* Log the actual 29-bit ID composed for the wire (bare-board proof). */
            uint32_t id = e32p_j1939_id_encode(6, DEMO_PGN, NODE_SA, E32P_J1939_ADDR_GLOBAL);
            ESP_LOGI(TAG, "tx PGN=%u SA=0x%02X DA=0xFF id=0x%08lX data[0]=%u ok",
                     DEMO_PGN, NODE_SA, (unsigned long)id, counter);
        } else {
            /* Without a bus, NO_ACK self-test can't drain its TX queue: the
               controller bit-monitors RX, sees no loopback, retransmits, and
               the queue fills. Expected on a bare board — say it once, then
               idle instead of spamming. The codec + TX above are the proof. */
            ESP_LOGW(TAG, "TX stalled after %u frames (%s). Expected without a bus: "
                          "connect a transceiver or a GPIO%d->%d jumper for live CAN. "
                          "Idling.", counter, esp_err_to_name(err), TX_GPIO, RX_GPIO);
            break;
        }

        /* Physical loopback: succeeds only with a TX->RX jumper / transceiver. */
        e32p_j1939_message_t rx = {0};
        err = e32p_j1939_receive(&rx, 100);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "rx PGN=%lu prio=%u SA=0x%02X DA=0x%02X len=%u data[0]=%u",
                     (unsigned long)rx.pgn, rx.priority, rx.src_addr, rx.dst_addr,
                     rx.len, rx.data[0]);
        } else if (!warned_no_rx) {
            ESP_LOGW(TAG, "no rx (%s) — expected without a GPIO%d->%d jumper; "
                          "TX path above is the bare-board proof",
                     esp_err_to_name(err), TX_GPIO, RX_GPIO);
            warned_no_rx = true;  /* say it once, don't spam */
        }

        counter++;
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    /* Quiet heartbeat after the bare-board TX burst. */
    for (;;) {
        ESP_LOGI(TAG, "idle — connect a transceiver or GPIO%d->%d jumper for live CAN",
                 TX_GPIO, RX_GPIO);
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
