/**
 * @file main.c
 * @brief 18_j1939tp_bam — proves e32p_j1939tp end-to-end on a bare board.
 *
 * Runs two transport engines in one app (sender SA 0x80, receiver SA 0x21)
 * wired together through an in-process frame queue — no CAN bus needed. Each
 * round it transfers a ~250-byte message by BAM (broadcast), the receiver
 * reassembles it, and the app verifies the bytes match and logs the result.
 *
 * The same engine wires to real CAN by pointing the tx sink at e32p_j1939_send
 * and feeding e32p_j1939_receive() frames into e32p_j1939tp_on_frame() (see the
 * component README); this demo uses the in-process loopback so it needs no
 * transceiver.
 */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "e32p_j1939tp.h"

static const char *TAG = "j1939tp_bam";

#define SENDER_SA   0x80u
#define RECEIVER_SA 0x21u
#define MSG_PGN     65259u   /* 0xFEEB, PDU2 */
#define MSG_LEN     250

static e32p_j1939tp_handle_t s_sender, s_receiver;

/* In-process frame queue: to==0 -> sender, to==1 -> receiver. */
static struct { e32p_j1939_message_t f; int to; } s_q[256];
static int s_qh, s_qt;

static volatile bool     s_rx_done;
static volatile uint16_t s_rx_len;
static uint8_t           s_rx_buf[512];
static volatile bool     s_tx_done;
static volatile esp_err_t s_tx_status;

static esp_err_t tx_from_sender(const e32p_j1939_message_t *m, void *u)
{
    (void)u; s_q[s_qt].f = *m; s_q[s_qt].to = 1; s_qt = (s_qt + 1) & 255; return ESP_OK;
}
static esp_err_t tx_from_receiver(const e32p_j1939_message_t *m, void *u)
{
    (void)u; s_q[s_qt].f = *m; s_q[s_qt].to = 0; s_qt = (s_qt + 1) & 255; return ESP_OK;
}
static void on_rx(uint32_t pgn, uint8_t src, uint8_t dst,
                  const uint8_t *data, uint16_t len, void *u)
{
    (void)dst; (void)u;
    memcpy(s_rx_buf, data, len);
    s_rx_len = len; s_rx_done = true;
    ESP_LOGI(TAG, "receiver got PGN=%lu from SA=0x%02X, %u bytes",
             (unsigned long)pgn, src, len);
}
static void on_tx_done(uint32_t pgn, uint8_t dst, esp_err_t st, void *u)
{
    (void)pgn; (void)dst; (void)u; s_tx_done = true; s_tx_status = st;
}

static void pump(void)
{
    while (s_qh != s_qt) {
        e32p_j1939_message_t f = s_q[s_qh].f;
        int to = s_q[s_qh].to;
        s_qh = (s_qh + 1) & 255;
        e32p_j1939tp_on_frame(to == 0 ? s_sender : s_receiver, &f);
    }
}

void app_main(void)
{
    e32p_j1939tp_config_t cs = { .src_addr = SENDER_SA, .tx = tx_from_sender,
                                 .on_rx = on_rx, .on_tx_done = on_tx_done };
    e32p_j1939tp_config_t cr = { .src_addr = RECEIVER_SA, .tx = tx_from_receiver,
                                 .on_rx = on_rx };
    ESP_ERROR_CHECK(e32p_j1939tp_init(&cs, &s_sender));
    ESP_ERROR_CHECK(e32p_j1939tp_init(&cr, &s_receiver));
    ESP_LOGI(TAG, "two TP engines (SA 0x%02X -> 0x%02X), BAM %d-byte transfers",
             SENDER_SA, RECEIVER_SA, MSG_LEN);

    uint8_t seed = 0;
    for (;;) {
        /* Build a message with a rolling pattern so we can verify reassembly. */
        uint8_t msg[MSG_LEN];
        for (int i = 0; i < MSG_LEN; i++) msg[i] = (uint8_t)(i + seed);

        s_qh = s_qt = 0;
        s_rx_done = false; s_rx_len = 0; s_tx_done = false;

        ESP_ERROR_CHECK(e32p_j1939tp_send(s_sender, MSG_PGN, 6,
                                          E32P_J1939_ADDR_GLOBAL, msg, MSG_LEN));

        /* Drive both engines with a monotonic ms clock until the receiver
           completes. Poll BOTH engines first (refreshing their internal clocks)
           and only THEN pump frames into them: on_frame() stamps a session's
           timer with the engine's last poll() time, so it must be current when a
           frame is delivered. (A real CAN task polls continuously, so its cached
           time is never stale; here the 2 s idle between transfers would be.) */
        bool ok = false;
        for (int step = 0; step < 400 && !s_rx_done; step++) {
            uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
            e32p_j1939tp_poll(s_sender, now);
            e32p_j1939tp_poll(s_receiver, now);
            pump();
            vTaskDelay(pdMS_TO_TICKS(10));       /* ~10 ms/tick; BAM paces at 50 ms */
        }

        if (s_rx_done && s_rx_len == MSG_LEN && memcmp(msg, s_rx_buf, MSG_LEN) == 0) {
            ok = true;
        }
        ESP_LOGI(TAG, "BAM transfer #%u: %s (%u bytes%s)", seed, ok ? "OK" : "FAILED",
                 s_rx_len, ok ? ", bytes match" : "");

        seed++;
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
