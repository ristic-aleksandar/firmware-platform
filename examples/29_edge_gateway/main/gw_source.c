/**
 * @file gw_source.c
 * @brief The two field-data backends behind gw_source.h.
 *
 * `sim` fabricates four J1939-shaped engine signals that drift slowly over time
 * so the cloud pipeline shows live, changing data with no hardware. `j1939`
 * reads a real CAN bus in LISTEN_ONLY mode (a gateway must never disturb the
 * bus it taps) and decodes a couple of well-known PGNs into the same sample
 * shape. Both are selected at startup by main.c.
 */
#include "gw_source.h"
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "e32p_twai.h"
#include "e32p_j1939.h"

/* Pins default to the same values as the Kconfig entries; the guards keep the
   file compiling even if those symbols are ever removed. */
#ifndef CONFIG_GW_CAN_TX_GPIO
#define CONFIG_GW_CAN_TX_GPIO 5
#endif
#ifndef CONFIG_GW_CAN_RX_GPIO
#define CONFIG_GW_CAN_RX_GPIO 4
#endif

static const char *TAG = "gw_src";

/* --- Simulator ------------------------------------------------------------- */

static esp_err_t sim_start(void)
{
    return ESP_OK;
}

/* Produce four signals whose values drift on a ~31 s cycle. */
static int sim_poll(gw_sample_t *out, int max)
{
    if (!out || max < 4) {
        return -1;
    }
    double t = (double)esp_timer_get_time() / 1e6;        /* seconds */
    float osc = (float)((sin(t / 5.0) + 1.0) * 0.5);       /* 0..1 */

    out[0] = (gw_sample_t){ .pgn = 61444, .name = "engine_speed",
                            .value = 700.0f + osc * 1800.0f, .unit = "rpm" };
    out[1] = (gw_sample_t){ .pgn = 65262, .name = "coolant_temp",
                            .value = 70.0f + osc * 25.0f, .unit = "degC" };
    out[2] = (gw_sample_t){ .pgn = 65263, .name = "oil_pressure",
                            .value = 250.0f + osc * 150.0f, .unit = "kPa" };
    out[3] = (gw_sample_t){ .pgn = 65276, .name = "fuel_level",
                            .value = 100.0f - osc * 40.0f, .unit = "pct" };
    return 4;
}

static const gw_source_t SIM = { .kind = "sim", .start = sim_start, .poll = sim_poll };

const gw_source_t *gw_source_sim(void)
{
    return &SIM;
}

/* --- Real J1939 / CAN ------------------------------------------------------ */

/* A background task drains the bus continuously into this latest-value table,
   so telemetry (which polls only every few seconds) never misses the slower PGN:
   ET1 arrives ~1 Hz while EEC1 arrives ~10 Hz, and the RX queue is small, so a
   poll-time-only reader would fill up on EEC1 and drop coolant. The task keeps
   the newest value per signal; poll() just snapshots it. */
static portMUX_TYPE s_j1939_mux = portMUX_INITIALIZER_UNLOCKED;
static gw_sample_t  s_rpm, s_cool;
static bool         s_have_rpm, s_have_cool;

static void j1939_rx_task(void *arg)
{
    (void)arg;
    for (;;) {
        e32p_j1939_message_t m;
        if (e32p_j1939_receive(&m, 1000) != ESP_OK) {
            continue;   /* idle bus / timeout: keep waiting */
        }
        if (m.pgn == 61444 && m.len >= 5) {           /* EEC1: engine speed, SPN 190 */
            uint16_t raw = (uint16_t)m.data[3] | ((uint16_t)m.data[4] << 8);
            gw_sample_t s = { .pgn = m.pgn, .name = "engine_speed",
                              .value = raw * 0.125f, .unit = "rpm" };
            portENTER_CRITICAL(&s_j1939_mux);
            s_rpm = s; s_have_rpm = true;
            portEXIT_CRITICAL(&s_j1939_mux);
        } else if (m.pgn == 65262 && m.len >= 1) {    /* ET1: coolant temp, SPN 110 */
            gw_sample_t s = { .pgn = m.pgn, .name = "coolant_temp",
                              .value = (float)m.data[0] - 40.0f, .unit = "degC" };
            portENTER_CRITICAL(&s_j1939_mux);
            s_cool = s; s_have_cool = true;
            portEXIT_CRITICAL(&s_j1939_mux);
        }
        /* Unknown PGNs are ignored for v1 — extend the table as needed. */
    }
}

static esp_err_t j1939_start(void)
{
    e32p_twai_config_t tcfg = {
        .tx_gpio     = CONFIG_GW_CAN_TX_GPIO,
        .rx_gpio     = CONFIG_GW_CAN_RX_GPIO,
        .bitrate_bps = 250000,                    /* J1939 standard rate */
        .mode        = E32P_TWAI_MODE_LISTEN_ONLY,/* read-only tap: never ACK/drive */
    };
    esp_err_t err = e32p_twai_init(&tcfg);
    if (err != ESP_OK) {
        return err;
    }
    err = e32p_twai_start();
    if (err != ESP_OK) {
        return err;
    }
    /* SA is cosmetic in listen-only (we never transmit). */
    e32p_j1939_config_t jcfg = { .src_addr = 0xF9, .default_priority = 6,
                                 .self_reception = false };
    err = e32p_j1939_init(&jcfg);
    if (err != ESP_OK) {
        return err;
    }
    if (xTaskCreate(j1939_rx_task, "gw_j1939_rx", 3072, NULL, 6, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

/* Snapshot the latest value of each signal the drain task has seen. */
static int j1939_poll(gw_sample_t *out, int max)
{
    if (!out || max < 1) {
        return -1;
    }
    int n = 0;
    portENTER_CRITICAL(&s_j1939_mux);
    if (s_have_rpm  && n < max) { out[n++] = s_rpm;  }
    if (s_have_cool && n < max) { out[n++] = s_cool; }
    portEXIT_CRITICAL(&s_j1939_mux);
    (void)TAG;
    return n;
}

static const gw_source_t J1939 = { .kind = "j1939", .start = j1939_start, .poll = j1939_poll };

const gw_source_t *gw_source_j1939(void)
{
    return &J1939;
}
