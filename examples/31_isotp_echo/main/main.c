/**
 * @file main.c
 * @brief 31_isotp_echo — ISO-TP (ISO 15765-2) segmented send, reassemble, echo.
 *
 * Pick the role in menuconfig (31_isotp_echo -> Node role):
 *
 *   LOOPBACK  Both ends in one app, wired through an in-process frame queue.
 *             One board, no transceiver. Proves the engine end to end.
 *   NODE_A    Tester on a real bus. Sends CONFIG_EX31_PAYLOAD_LEN bytes to the
 *             ECU and checks the echo is byte-exact.
 *   NODE_B    ECU on a real bus. Reassembles each request and echoes it back.
 *
 * Addressing follows the e32p_isotp Kconfig choice: normal 11-bit uses
 * 0x7E0 (request) / 0x7E8 (response); normal-fixed 29-bit uses
 * 0x18DA10F1 / 0x18DAF110 (tester SA 0xF1, ECU SA 0x10, per ISO 15765-4).
 */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/twai.h"   /* raw status/recovery for bus-off handling */
#include "esp_timer.h"
#include "esp_log.h"
#include "e32p_time.h"
#include "e32p_twai.h"
#include "e32p_isotp.h"
#include "e32p_isotp_twai.h"

static const char *TAG = "isotp_echo";

#define REQ_ID          0x7E0u   /* tester -> ECU (normal 11-bit)  */
#define RESP_ID         0x7E8u   /* ECU -> tester (normal 11-bit)  */
#define TESTER_SA       0xF1u    /* ISO 15765-4 external test equipment */
#define ECU_SA          0x10u
#define ROUND_PERIOD_MS 2000u
#define XFER_TIMEOUT_MS 3000u
#define PAYLOAD_LEN     CONFIG_EX31_PAYLOAD_LEN

#if defined(CONFIG_E32P_ISOTP_ADDRESSING_NORMAL_FIXED)
#  define ADDR_DESC "normal-fixed 29-bit (0x18DA10F1 / 0x18DAF110)"
#else
#  define ADDR_DESC "normal 11-bit (0x7E0 / 0x7E8)"
#endif

/* The loops below must block, not spin. At CONFIG_FREERTOS_HZ=100 a 1 ms delay
   would round to 0 ticks and starve the idle task; this makes that a build
   error instead. */
E32P_ASSERT_BLOCKING_MS(1);

static uint8_t s_tx[PAYLOAD_LEN];
static uint8_t s_rx[PAYLOAD_LEN];

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/** @brief Rolling pattern, so a dropped or reordered frame shows up as a
 *         byte mismatch rather than passing by coincidence. */
static void fill_payload(uint8_t seed)
{
    for (int i = 0; i < PAYLOAD_LEN; i++) {
        s_tx[i] = (uint8_t)(i * 7 + seed);
    }
}

/* =========================== LOOPBACK (no bus) ========================== */
#if defined(CONFIG_EX31_ROLE_LOOPBACK)

static e32p_isotp_handle_t s_a, s_b;

/* In-process frame queue: to==0 -> A, to==1 -> B. */
static struct { e32p_twai_message_t f; int to; } s_q[256];
static int s_qh, s_qt;

static volatile bool     s_echo_done;
static volatile uint16_t s_echo_len;

static esp_err_t q_put(const e32p_twai_message_t *m, int to)
{
    const int next = (s_qt + 1) & 255;
    if (next == s_qh) {
        return ESP_FAIL;              /* full: back-pressure, engine retries */
    }
    s_q[s_qt].f = *m;
    s_q[s_qt].to = to;
    s_qt = next;
    return ESP_OK;
}

static esp_err_t tx_from_a(const e32p_twai_message_t *m, void *u) { (void)u; return q_put(m, 1); }
static esp_err_t tx_from_b(const e32p_twai_message_t *m, void *u) { (void)u; return q_put(m, 0); }

/** @brief A received the echo. */
static void a_on_rx(const uint8_t *d, uint16_t len, void *u)
{
    (void)u;
    memcpy(s_rx, d, (len <= PAYLOAD_LEN) ? len : PAYLOAD_LEN);
    s_echo_len  = len;
    s_echo_done = true;
}

/** @brief B reassembled a request: echo it. Replying from inside on_rx is
 *         allowed - send() only arms the session (D-02: one TX and one RX may
 *         be in flight on a channel at once). */
static void b_on_rx(const uint8_t *d, uint16_t len, void *u)
{
    (void)u;
    esp_err_t err = e32p_isotp_send(s_b, d, len);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "B echo send: %s", esp_err_to_name(err));
    }
}

/** @brief Deliver the frames pending at entry. Replies wait for the next pump,
 *         as they would on a real bus. */
static void pump(void)
{
    const int end = s_qt;
    while (s_qh != end) {
        e32p_twai_message_t f = s_q[s_qh].f;
        int to = s_q[s_qh].to;
        s_qh = (s_qh + 1) & 255;
        e32p_isotp_on_frame(to == 0 ? s_a : s_b, &f);
    }
}

static void run_role(void)
{
    e32p_isotp_config_t ca, cb;
    e32p_isotp_config_defaults(&ca, REQ_ID, RESP_ID, TESTER_SA, ECU_SA, false);
    ca.tx = tx_from_a;
    ca.on_rx = a_on_rx;
    ca.max_msg_bytes = E32P_ISOTP_MAX_PAYLOAD;
    ESP_ERROR_CHECK(e32p_isotp_init(&ca, &s_a));

    e32p_isotp_config_defaults(&cb, RESP_ID, REQ_ID, ECU_SA, TESTER_SA, false);
    cb.tx = tx_from_b;
    cb.on_rx = b_on_rx;
    cb.max_msg_bytes = E32P_ISOTP_MAX_PAYLOAD;
    ESP_ERROR_CHECK(e32p_isotp_init(&cb, &s_b));

    ESP_LOGI(TAG, "LOOPBACK: two engines, in-process queue, %d-byte payload, %s",
             PAYLOAD_LEN, ADDR_DESC);

    uint8_t  seed = 0;
    uint32_t pass = 0, fail = 0;
    for (;;) {
        fill_payload(seed);
        s_qh = s_qt = 0;
        s_echo_done = false;
        s_echo_len  = 0;

        /* Poll first: send() and on_frame() stamp session timers with the
           engine's last poll() time, which is ~2 s stale between rounds. */
        e32p_isotp_poll(s_a, now_ms());
        e32p_isotp_poll(s_b, now_ms());

        const int64_t t0 = esp_timer_get_time();
        esp_err_t err = e32p_isotp_send(s_a, s_tx, PAYLOAD_LEN);
        while (err == ESP_OK && !s_echo_done &&
               (esp_timer_get_time() - t0) < (int64_t)XFER_TIMEOUT_MS * 1000) {
            const uint32_t t = now_ms();
            e32p_isotp_poll(s_a, t);
            e32p_isotp_poll(s_b, t);
            pump();
            vTaskDelay(pdMS_TO_TICKS(1));
        }
        const uint32_t dt = (uint32_t)((esp_timer_get_time() - t0) / 1000);

        const bool ok = err == ESP_OK && s_echo_done && s_echo_len == PAYLOAD_LEN &&
                        memcmp(s_tx, s_rx, PAYLOAD_LEN) == 0;
        if (ok) {
            pass++;
            ESP_LOGI(TAG, "PASS #%u len=%d t=%lums (pass %lu / fail %lu)", seed,
                     PAYLOAD_LEN, (unsigned long)dt, (unsigned long)pass, (unsigned long)fail);
        } else {
            fail++;
            ESP_LOGE(TAG, "FAIL #%u: %s (echo %s, %u bytes)", seed,
                     err != ESP_OK ? esp_err_to_name(err) : "no match",
                     s_echo_done ? "received" : "missing", s_echo_len);
        }
        seed++;
        vTaskDelay(pdMS_TO_TICKS(ROUND_PERIOD_MS));
    }
}

/* ========================== NODE A / NODE B (bus) ======================= */
#else

/**
 * @brief Recover from bus-off. A NORMAL-mode node with no acking peer retries
 *        its frame until the error counters push it to bus-off, and TWAI does
 *        not come back on its own. Same recovery the HIL companion uses.
 */
static void bus_keepalive(void)
{
    twai_status_info_t st;
    if (twai_get_status_info(&st) != ESP_OK) {
        return;
    }
    /* Error counters are the physical-layer diagnosis. A transmitter nobody
       acknowledges climbs TEC with REC at 0. A node seeing bits it cannot
       decode (wrong bitrate, bad signal) climbs REC and bus_err. A node that
       is not on the bus at all shows nothing. */
    static uint32_t s_last_bus_err;
    if (st.bus_error_count != s_last_bus_err) {
        ESP_LOGW(TAG, "twai: state=%d tec=%lu rec=%lu bus_err=%lu tx_failed=%lu",
                 (int)st.state, (unsigned long)st.tx_error_counter,
                 (unsigned long)st.rx_error_counter, (unsigned long)st.bus_error_count,
                 (unsigned long)st.tx_failed_count);
        s_last_bus_err = st.bus_error_count;
    }
    if (st.state == TWAI_STATE_BUS_OFF) {
        ESP_LOGW(TAG, "bus-off - is the other node running and the bus terminated? recovering");
        twai_initiate_recovery();
    } else if (st.state == TWAI_STATE_STOPPED) {
        twai_start();                  /* recovery finished; resume */
    }
}

static void bus_up(void)
{
    e32p_twai_config_t bus = {
        .tx_gpio     = CONFIG_EX31_CAN_TX_GPIO,
        .rx_gpio     = CONFIG_EX31_CAN_RX_GPIO,
        .bitrate_bps = E32P_ISOTP_DEFAULT_BITRATE,
        .mode        = E32P_TWAI_MODE_NORMAL,  /* both nodes ACK each other */
    };
    ESP_ERROR_CHECK(e32p_twai_init(&bus));
    ESP_ERROR_CHECK(e32p_twai_start());
    ESP_ERROR_CHECK(e32p_isotp_twai_start(NULL));
}

#if defined(CONFIG_EX31_ROLE_NODE_A)

static void run_role(void)
{
    bus_up();

    e32p_isotp_config_t c;
    e32p_isotp_config_defaults(&c, REQ_ID, RESP_ID, TESTER_SA, ECU_SA, false);
    c.max_msg_bytes = E32P_ISOTP_MAX_PAYLOAD;     /* on_rx NULL -> sync mode */
    e32p_isotp_handle_t ch;
    ESP_ERROR_CHECK(e32p_isotp_twai_channel(&c, &ch));

    ESP_LOGI(TAG, "NODE A (tester): %d-byte payload, %lu bit/s, %s", PAYLOAD_LEN,
             (unsigned long)E32P_ISOTP_DEFAULT_BITRATE, ADDR_DESC);

    uint8_t  seed = 0;
    uint32_t pass = 0, fail = 0;
    for (;;) {
        bus_keepalive();

        /* Discard a late echo from a round that already timed out, so it is
           not mistaken for this round's reply. */
        uint16_t n = 0;
        while (e32p_isotp_twai_recv_sync(ch, s_rx, sizeof(s_rx), &n, 0) == ESP_OK) { }

        fill_payload(seed);
        const int64_t t0 = esp_timer_get_time();
        esp_err_t err = e32p_isotp_twai_send_sync(ch, s_tx, PAYLOAD_LEN, XFER_TIMEOUT_MS);
        if (err == ESP_OK) {
            err = e32p_isotp_twai_recv_sync(ch, s_rx, sizeof(s_rx), &n, XFER_TIMEOUT_MS);
        }
        const uint32_t dt = (uint32_t)((esp_timer_get_time() - t0) / 1000);

        const bool ok = err == ESP_OK && n == PAYLOAD_LEN &&
                        memcmp(s_tx, s_rx, PAYLOAD_LEN) == 0;
        if (ok) {
            pass++;
            ESP_LOGI(TAG, "PASS #%u len=%d t=%lums (pass %lu / fail %lu)", seed,
                     PAYLOAD_LEN, (unsigned long)dt, (unsigned long)pass, (unsigned long)fail);
        } else {
            fail++;
            if (err == ESP_ERR_TIMEOUT) {
                ESP_LOGE(TAG, "FAIL #%u: timeout - is node B running, and the bus "
                              "terminated (2x120R)?", seed);
            } else if (err == ESP_ERR_INVALID_STATE) {
                ESP_LOGE(TAG, "FAIL #%u: controller still recovering from bus-off", seed);
            } else if (err != ESP_OK) {
                ESP_LOGE(TAG, "FAIL #%u: %s", seed, esp_err_to_name(err));
            } else {
                ESP_LOGE(TAG, "FAIL #%u: echo %u bytes, %s", seed, n,
                         n == PAYLOAD_LEN ? "bytes differ" : "length differs");
            }
        }
        seed++;
        vTaskDelay(pdMS_TO_TICKS(ROUND_PERIOD_MS));
    }
}

#elif defined(CONFIG_EX31_ROLE_NODE_B)

static e32p_isotp_handle_t s_ch;
static volatile uint32_t   s_echoes;
static volatile uint16_t   s_last_len;

/** @brief Runs in the pump task. Kept free of logging: a UART write here
 *         would stall the pump in the middle of a transfer. */
static void on_request(const uint8_t *d, uint16_t len, void *u)
{
    (void)u;
    if (e32p_isotp_send(s_ch, d, len) == ESP_OK) {
        s_last_len = len;
        s_echoes++;
    }
}

static void run_role(void)
{
    bus_up();

    e32p_isotp_config_t c;
    e32p_isotp_config_defaults(&c, RESP_ID, REQ_ID, ECU_SA, TESTER_SA, false);
    c.on_rx = on_request;                         /* server mode */
    c.max_msg_bytes = E32P_ISOTP_MAX_PAYLOAD;
    ESP_ERROR_CHECK(e32p_isotp_twai_channel(&c, &s_ch));

    ESP_LOGI(TAG, "NODE B (ECU): echoing, %lu bit/s, %s",
             (unsigned long)E32P_ISOTP_DEFAULT_BITRATE, ADDR_DESC);

    uint32_t reported = 0;
    for (;;) {
        bus_keepalive();
        const uint32_t n = s_echoes;
        if (n != reported) {
            ESP_LOGI(TAG, "echoed %lu payload(s), last %u bytes",
                     (unsigned long)n, s_last_len);
            reported = n;
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

#endif  /* NODE_A / NODE_B */
#endif  /* LOOPBACK */

void app_main(void)
{
    run_role();
}
