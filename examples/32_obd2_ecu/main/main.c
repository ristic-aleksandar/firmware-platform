/**
 * @file main.c
 * @brief 32_obd2_ecu — an OBD-II ECU a scan tool can talk to, and a tester that
 *        talks to it.
 *
 * ECU role: live data on a slow sine (coolant, RPM, speed), a VIN, and a trouble
 * code stored the first time the simulated engine overheats — which also freezes
 * a frame, so mode 0x02 has something real to answer. Codes survive a reset.
 *
 * Tester role: the questions a scan tool asks — supported PIDs, three live
 * values, the VIN, and the stored codes — printed decoded once a second.
 *
 * Both roles are the same binary; pick one in menuconfig (see README.md).
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "e32p_nvs.h"
#include "e32p_twai.h"
#include "e32p_isotp.h"
#include "e32p_isotp_twai.h"
#include "e32p_obd2.h"
#include "e32p_obd2_isotp.h"

static const char *TAG = "ex32";

#define ECU_ADDR   CONFIG_EX32_ECU_ADDR
#define TESTER_SA  0xF1u
#define REQ_ID     (0x7E0u + (ECU_ADDR - 0x10u))
#define RESP_ID    (0x7E8u + (ECU_ADDR - 0x10u))
#define VIN_TEXT   "E32PDEMO000000001"
#define OVERHEAT_C 92.0f
#define DTC_P0217  0x0217u      /* engine over-temperature condition */

/* ================================= ECU =================================== */

#if CONFIG_EX32_ROLE_ECU

/* ---- simulated engine: only the ECU role has one ---- */

typedef struct {
    float coolant_c;
    float rpm;
    float speed_kph;
} engine_t;

static engine_t s_engine = { .coolant_c = 70.0f, .rpm = 800.0f, .speed_kph = 0.0f };

/** @brief Drift the signals on a slow (~31 s) sine, like example 30 does. */
static void engine_tick(void)
{
    const float osc = sinf((float)(esp_timer_get_time() / 1000) / 5000.0f);
    s_engine.coolant_c = 70.0f + osc * 25.0f;      /* 45..95 degC   */
    s_engine.rpm       = 1600.0f + osc * 900.0f;   /* 700..2500 rpm */
    s_engine.speed_kph = 60.0f + osc * 40.0f;      /* 20..100 km/h  */
}

static const e32p_obd2_pid_t k_pids[] = {
    { 0x05, 1 },   /* engine coolant temperature */
    { 0x0C, 2 },   /* engine RPM                 */
    { 0x0D, 1 },   /* vehicle speed              */
    { 0x1C, 1 },   /* OBD standard               */
};

/** @brief Hand the service layer the current value of one PID. */
static esp_err_t read_pid(uint8_t pid, uint8_t *out, uint8_t len, void *user)
{
    const engine_t *e = user;
    (void)len;
    switch (pid) {
    case 0x05:
        out[0] = (uint8_t)(e->coolant_c + 40.0f);            /* A - 40 degC */
        return ESP_OK;
    case 0x0C: {
        const uint16_t quarters = (uint16_t)(e->rpm * 4.0f); /* (256A+B)/4  */
        out[0] = (uint8_t)(quarters >> 8);
        out[1] = (uint8_t)(quarters & 0xFFu);
        return ESP_OK;
    }
    case 0x0D:
        out[0] = (uint8_t)e->speed_kph;
        return ESP_OK;
    default:
        return ESP_FAIL;                                      /* answered as unsupported */
    }
}

static void ecu_task(void *arg)
{
    e32p_obd2_handle_t server = arg;
    bool overheated = false;

    for (;;) {
        engine_tick();
        if (!overheated && s_engine.coolant_c > OVERHEAT_C) {
            /* A real fault: store the code, which also freezes the frame with
               the values the engine had at this moment. */
            const esp_err_t err = e32p_obd2_dtc_set(server, DTC_P0217, true);
            ESP_LOGW(TAG, "coolant %.1f C -> storing P0217 (%s)", s_engine.coolant_c,
                     esp_err_to_name(err));
            overheated = true;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

static void run_ecu(void)
{
    e32p_obd2_config_t cfg = {
        .pids = k_pids, .pid_count = sizeof(k_pids) / sizeof(k_pids[0]),
        .on_read = read_pid, .user = &s_engine,
        .vin = VIN_TEXT, .obd_standard = 0x06u,     /* 0x06 = EOBD */
        .nvs_namespace = "obd2",
    };
    e32p_obd2_handle_t server = NULL;
    ESP_ERROR_CHECK(e32p_obd2_init(&cfg, &server));
    ESP_LOGI(TAG, "stored codes at boot: %u confirmed, %u pending",
             (unsigned)e32p_obd2_dtc_count(server, true),
             (unsigned)e32p_obd2_dtc_count(server, false));

    const e32p_obd2_isotp_config_t bind = { .server = server, .ecu_addr = ECU_ADDR };
    ESP_ERROR_CHECK(e32p_obd2_isotp_start(&bind));
    ESP_LOGI(TAG, "ECU ready — ask me for 01 00, 01 0C, 09 02 or 03");

    xTaskCreate(ecu_task, "ex32_ecu", 3072, server, 5, NULL);
}

#endif /* CONFIG_EX32_ROLE_ECU */

/* ================================ tester ================================= */

#if CONFIG_EX32_ROLE_TESTER

static e32p_isotp_handle_t s_tester;

/**
 * @brief Send one request and wait for the answer.
 * @return response length, or 0 when nothing came back
 */
static uint16_t request(const uint8_t *req, uint16_t req_len, uint8_t *resp, uint16_t cap)
{
    esp_err_t err = e32p_isotp_twai_send_sync(s_tester, req, req_len, 500);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "request %02X: send failed (%s)", req[0], esp_err_to_name(err));
        return 0;
    }
    uint16_t got = 0;
    err = e32p_isotp_twai_recv_sync(s_tester, resp, cap, &got, 500);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "request %02X: no reply (%s)", req[0], esp_err_to_name(err));
        return 0;
    }
    return got;
}

static void print_dtcs(const uint8_t *resp, uint16_t len)
{
    static const char kind[4] = { 'P', 'C', 'B', 'U' };
    if (len < 2u || resp[0] != 0x43u) {
        printf("  codes    : (no answer)\n");
        return;
    }
    const uint8_t count = resp[1];
    if (count == 0u) {
        printf("  codes    : none\n");
        return;
    }
    printf("  codes    :");
    for (uint8_t i = 0; i < count && (uint16_t)(2u + i * 2u + 1u) < len; i++) {
        const uint8_t hi = resp[2u + i * 2u];
        const uint8_t lo = resp[3u + i * 2u];
        printf(" %c%u%X%02X", kind[(hi >> 6) & 0x03u], (unsigned)((hi >> 4) & 0x03u),
               (unsigned)(hi & 0x0Fu), lo);
    }
    printf("\n");
}

static void tester_task(void *arg)
{
    (void)arg;
    uint8_t resp[128];

    for (;;) {
        printf("--- OBD-II round ---\n");

        const uint8_t supported[] = { 0x01, 0x00 };
        uint16_t n = request(supported, sizeof(supported), resp, sizeof(resp));
        if (n >= 6u) {
            printf("  supported: %02X %02X %02X %02X\n", resp[2], resp[3], resp[4], resp[5]);
        } else {
            printf("  supported: (no answer — is the ECU powered and the bus wired?)\n");
        }

        const uint8_t coolant[] = { 0x01, 0x05 };
        n = request(coolant, sizeof(coolant), resp, sizeof(resp));
        if (n >= 3u) {
            printf("  coolant  : %d C\n", (int)resp[2] - 40);
        }

        const uint8_t rpm[] = { 0x01, 0x0C };
        n = request(rpm, sizeof(rpm), resp, sizeof(resp));
        if (n >= 4u) {
            printf("  rpm      : %u\n", (unsigned)(((resp[2] << 8) | resp[3]) / 4u));
        }

        const uint8_t speed[] = { 0x01, 0x0D };
        n = request(speed, sizeof(speed), resp, sizeof(resp));
        if (n >= 3u) {
            printf("  speed    : %u km/h\n", (unsigned)resp[2]);
        }

        const uint8_t vin[] = { 0x09, 0x02 };
        n = request(vin, sizeof(vin), resp, sizeof(resp));
        if (n >= 3u + E32P_OBD2_VIN_LEN) {
            printf("  vin      : %.*s\n", (int)E32P_OBD2_VIN_LEN, (const char *)&resp[3]);
        }

        const uint8_t dtc[] = { 0x03 };
        n = request(dtc, sizeof(dtc), resp, sizeof(resp));
        print_dtcs(resp, n);

        vTaskDelay(pdMS_TO_TICKS(CONFIG_EX32_POLL_MS));
    }
}

static void run_tester(void)
{
    e32p_isotp_config_t cfg;
    /* The tester transmits on the request ID and listens on the response ID -
       the mirror of the ECU's channel. */
    e32p_isotp_config_defaults(&cfg, REQ_ID, RESP_ID, TESTER_SA, (uint8_t)ECU_ADDR, false);
    cfg.max_msg_bytes = 128;
    ESP_ERROR_CHECK(e32p_isotp_twai_channel(&cfg, &s_tester));
    ESP_LOGI(TAG, "tester ready — polling every %d ms", CONFIG_EX32_POLL_MS);
    xTaskCreate(tester_task, "ex32_tester", 4096, NULL, 5, NULL);
}

#endif /* CONFIG_EX32_ROLE_TESTER */

/* ================================= boot ================================== */

void app_main(void)
{
    ESP_ERROR_CHECK(e32p_nvs_init());

    e32p_twai_config_t bus = { .tx_gpio = CONFIG_EX32_CAN_TX_GPIO,
                               .rx_gpio = CONFIG_EX32_CAN_RX_GPIO,
                               .bitrate_bps = E32P_ISOTP_DEFAULT_BITRATE,
                               .mode = E32P_TWAI_MODE_NORMAL };
    ESP_ERROR_CHECK(e32p_twai_init(&bus));
    ESP_ERROR_CHECK(e32p_twai_start());
    ESP_ERROR_CHECK(e32p_isotp_twai_start(NULL));
    ESP_LOGI(TAG, "bus up at %d bit/s on tx=%d rx=%d", (int)E32P_ISOTP_DEFAULT_BITRATE,
             CONFIG_EX32_CAN_TX_GPIO, CONFIG_EX32_CAN_RX_GPIO);

#if CONFIG_EX32_ROLE_ECU
    run_ecu();
#else
    run_tester();
#endif
}
