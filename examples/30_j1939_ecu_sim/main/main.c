/**
 * @file main.c
 * @brief 30_j1939_ecu_sim — a J1939 engine-ECU simulator.
 *
 * Models a real engine ECU: a NORMAL-mode TWAI node that *broadcasts* the two
 * PGNs the 29_edge_gateway flagship decodes, at their usual cyclic rates:
 *
 *   PGN 65262 (ET1,  SPN 110) coolant temp   -> byte0 = degC + 40      @ 1 Hz
 *   PGN 61444 (EEC1, SPN 190) engine speed    -> bytes3-4 = rpm/0.125   @ 100 ms
 *
 * The values drift on a slow sine so the gateway's cloud telemetry visibly
 * moves. This is the bench signal source for WIRING.md §9 (real-J1939 path) and
 * a reusable J1939 node for any CAN demo — it keeps the gateway listen-only and
 * the HIL agent untouched.
 *
 * ACK note: the gateway that consumes these frames is a LISTEN_ONLY tap, so it
 * never ACKs. With no acking peer on the bench, a NORMAL node would error-count
 * straight to bus-off, so this node runs in NO_ACK mode — it still drives the
 * frames onto the wire for the tap to read, it just doesn't require the ACK bit.
 * The bus-off recovery below stays as a cheap safety net for any other bus fault.
 * (driver/twai.h is used directly for the recovery calls e32p_twai doesn't wrap yet.)
 *
 * Wiring: this board -> its CAN transceiver on CONFIG_ECU_CAN_TX/RX_GPIO
 * (default 21/22 WROOM; 4/5 on an S3), transceiver onto the CANH/CANL bus with
 * 120 Ohm at each end and a common ground. See tests/hil/WIRING.md §7.1 + §9.
 */
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/twai.h"
#include "e32p_twai.h"
#include "e32p_j1939.h"
#include "sdkconfig.h"

static const char *TAG = "j1939_ecu";

#define ECU_SRC_ADDR   0x00      /* engine ECU #1 is source address 0 */
#define PGN_EEC1       61444u    /* engine speed  (priority 3, ~100 ms) */
#define PGN_ET1        65262u    /* engine temp 1 (priority 6, ~1 s)    */
#define TICK_MS        100       /* base loop period; EEC1 every tick, ET1 every 10 */

/* Recover the controller if it has error-counted into bus-off (no ACK because no
   peer is on the bus yet). Rate-limit the log so a solo board isn't noisy. */
static void ecu_recover_bus_off(void)
{
    twai_status_info_t st;
    if (twai_get_status_info(&st) != ESP_OK || st.state != TWAI_STATE_BUS_OFF) {
        return;
    }
    static int64_t last_warn_us = 0;
    int64_t now = esp_timer_get_time();
    if (now - last_warn_us > 3000000) {   /* at most once per 3 s */
        ESP_LOGW(TAG, "bus-off — no ACK (is the gateway on the bus?); recovering");
        last_warn_us = now;
    }
    twai_initiate_recovery();
    for (int i = 0; i < 100; i++) {       /* recovery completes -> STOPPED */
        vTaskDelay(pdMS_TO_TICKS(10));
        if (twai_get_status_info(&st) == ESP_OK && st.state == TWAI_STATE_STOPPED) {
            break;
        }
    }
    twai_start();
}

/* Fill an unused J1939 byte with 0xFF ("not available"), the SAE convention. */
static void j1939_frame(e32p_j1939_message_t *m, uint32_t pgn, uint8_t priority)
{
    m->priority = priority;
    m->pgn      = pgn;
    m->dst_addr = E32P_J1939_ADDR_GLOBAL;
    m->len      = 8;
    for (int i = 0; i < 8; i++) {
        m->data[i] = 0xFF;
    }
}

void app_main(void)
{
    e32p_twai_config_t tcfg = { .tx_gpio     = CONFIG_ECU_CAN_TX_GPIO,
                                .rx_gpio     = CONFIG_ECU_CAN_RX_GPIO,
                                .bitrate_bps = 250000,               /* J1939 standard rate */
                                .mode        = E32P_TWAI_MODE_NO_ACK };  /* listen-only tap won't ACK */
    ESP_ERROR_CHECK(e32p_twai_init(&tcfg));
    ESP_ERROR_CHECK(e32p_twai_start());

    e32p_j1939_config_t jcfg = { .src_addr = ECU_SRC_ADDR, .default_priority = 6,
                                 .self_reception = false };
    ESP_ERROR_CHECK(e32p_j1939_init(&jcfg));
    ESP_LOGI(TAG, "J1939 ECU sim up: SA=0x%02X, CAN TX=%d RX=%d @250k — broadcasting ET1+EEC1",
             ECU_SRC_ADDR, CONFIG_ECU_CAN_TX_GPIO, CONFIG_ECU_CAN_RX_GPIO);

    uint32_t tick = 0;
    for (;;) {
        ecu_recover_bus_off();

        /* Drift both signals on a slow (~31 s) sine so the cloud data moves. */
        double t   = (double)esp_timer_get_time() / 1e6;
        float  osc = (float)((sin(t / 5.0) + 1.0) * 0.5);   /* 0..1 */
        float  coolant_c = 70.0f + osc * 25.0f;             /* 70..95 degC */
        float  rpm       = 700.0f + osc * 1800.0f;          /* 700..2500 rpm */

        /* EEC1 every tick (~100 ms): engine speed in bytes 3-4, 0.125 rpm/bit LE. */
        e32p_j1939_message_t eec1;
        j1939_frame(&eec1, PGN_EEC1, 3);
        uint16_t raw_rpm = (uint16_t)(rpm / 0.125f);
        eec1.data[3] = (uint8_t)(raw_rpm & 0xFF);
        eec1.data[4] = (uint8_t)(raw_rpm >> 8);
        e32p_j1939_send(&eec1, 20);

        /* ET1 every 10 ticks (~1 s): coolant temp in byte 0, 1 degC/bit, -40 offset. */
        if (tick % 10 == 0) {
            e32p_j1939_message_t et1;
            j1939_frame(&et1, PGN_ET1, 6);
            et1.data[0] = (uint8_t)(coolant_c + 40.0f);
            e32p_j1939_send(&et1, 20);
            ESP_LOGI(TAG, "ET1 coolant=%.0f C   EEC1 rpm=%.0f", coolant_c, rpm);
        }

        tick++;
        vTaskDelay(pdMS_TO_TICKS(TICK_MS));
    }
}
