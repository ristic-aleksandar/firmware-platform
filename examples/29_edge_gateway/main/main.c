/**
 * @file main.c
 * @brief 29_edge_gateway — an industrial edge gateway (flagship integration demo).
 *
 * DEMO ONLY. The MQTT link is plaintext and anonymous, BLE onboarding is Just Works,
 * and there is no signed OTA. The build stops with #error when CONFIG_E32P_BENCH_FORBIDDEN
 * is set (product build, Secure Boot or Flash Encryption).
 *
 * One device, one lifecycle, ~14 e32p_* modules working together:
 *
 *   provision (BLE) -> acquire (J1939/CAN or sim) -> report (MQTT) -> be
 *   controlled (MQTT commands) -> repeat.
 *
 * Boot decides the mode:
 *   - UNPROVISIONED (no SSID/broker in NVS): run BLE provisioning, save, reboot.
 *   - PROVISIONED: bring up Wi-Fi + MQTT, read the field source, and publish
 *     normalized JSON telemetry; accept commands on gw/<id>/cmd.
 *
 * BLE and Wi-Fi run in separate boots (a reboot sits between provisioning and
 * normal operation), so the two radios never contend for RAM at the same time.
 *
 * Telemetry  ->  gw/<id>/telemetry   {"gw","up_ms","heap","rssi","src","signals":[...]}
 * Events     ->  gw/<id>/evt         "online"
 * Commands   <-  gw/<id>/cmd         "identify" | "interval <s>" | "reboot"
 *                                    (no sender check: any MQTT client can send them)
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "e32p_nvs.h"
#include "e32p_wifi.h"
#include "e32p_mqtt.h"
#include "e32p_ota.h"
#include "e32p_wdt.h"
#include "e32p_sysinfo.h"
#include "e32p_gpio.h"
#include "e32p_tm1637.h"
#include "gw_config.h"
#include "gw_source.h"
#include "gw_provision.h"

#if CONFIG_E32P_BENCH_FORBIDDEN
#error "29_edge_gateway is a demo: open MQTT commands, Just Works BLE onboarding, " \
       "no signed OTA (CONFIG_E32P_BENCH_FORBIDDEN=y)"
#endif

static const char *TAG = "gateway";

#define LED_PIN  CONFIG_GW_STATUS_LED_GPIO

static gw_config_t          s_cfg;
static const gw_source_t   *s_src;
static e32p_mqtt_handle_t    s_mqtt;
static e32p_tm1637_handle_t  s_disp;        /* NULL if no display present */

static char s_topic_tel[64];
static char s_topic_cmd[64];
static char s_topic_evt[64];

/* Pending actions raised in the MQTT callback, serviced by the main loop so the
   esp-mqtt task never blocks on a long operation. */
static volatile bool s_identify;
static volatile bool s_reboot;

/* --- status LED ------------------------------------------------------------ */

static void led_init(void)
{
    e32p_gpio_config_t c = { .pin = LED_PIN, .mode = E32P_GPIO_MODE_OUTPUT,
                             .pull = E32P_GPIO_PULL_NONE, .intr = E32P_GPIO_INTR_NONE };
    e32p_gpio_config(&c);
}

static void led_set(bool on)
{
    e32p_gpio_set_level(LED_PIN, on ? 1 : 0);
}

/* --- telemetry ------------------------------------------------------------- */

/* Append one signal object; floats are printed via integer math so the build
   works regardless of newlib float-printf settings. */
static int append_signal(char *buf, size_t cap, int len, bool first, const gw_sample_t *s)
{
    int whole = (int)s->value;
    int frac  = (int)((s->value - (float)whole) * 10.0f);
    if (frac < 0) {
        frac = -frac;
    }
    return len + snprintf(buf + len, cap - (size_t)len,
        "%s{\"pgn\":%u,\"name\":\"%s\",\"value\":%d.%d,\"unit\":\"%s\"}",
        first ? "" : ",", (unsigned)s->pgn, s->name, whole, frac, s->unit);
}

static int build_telemetry(char *buf, size_t cap, const gw_sample_t *s, int n)
{
    int8_t rssi = 0;
    e32p_wifi_get_rssi(&rssi);
    int len = snprintf(buf, cap,
        "{\"gw\":\"%s\",\"up_ms\":%llu,\"heap\":%u,\"rssi\":%d,\"src\":\"%s\",\"signals\":[",
        s_cfg.id, (unsigned long long)e32p_sysinfo_uptime_ms(),
        (unsigned)e32p_sysinfo_free_heap(), (int)rssi, s_src->kind);
    for (int i = 0; i < n && len < (int)cap; i++) {
        len = append_signal(buf, cap, len, i == 0, &s[i]);
    }
    len += snprintf(buf + len, cap - (size_t)len, "]}");
    return len;
}

/* --- commands -------------------------------------------------------------- */

static void handle_cmd(const char *data, int len)
{
    char c[192];
    if (len <= 0 || len >= (int)sizeof(c)) {
        return;
    }
    memcpy(c, data, len);
    c[len] = '\0';

    char *verb = strtok(c, " \t\r\n");
    if (!verb) {
        return;
    }
    if (strcmp(verb, "identify") == 0) {
        s_identify = true;
    } else if (strcmp(verb, "interval") == 0) {
        char *arg = strtok(NULL, " \t\r\n");
        int v = arg ? atoi(arg) : 0;
        if (v >= 1 && v != s_cfg.interval_s) {   /* same value: no NVS write */
            s_cfg.interval_s = v;
            gw_config_save(&s_cfg);
            ESP_LOGI(TAG, "interval -> %d s", v);
        }
    } else if (strcmp(verb, "reboot") == 0) {
        s_reboot = true;
    } else {
        ESP_LOGW(TAG, "unknown cmd: %s", verb);
    }
}

static void on_mqtt(const e32p_mqtt_event_t *ev, void *user)
{
    (void)user;
    switch (ev->type) {
    case E32P_MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "broker connected; subscribing %s", s_topic_cmd);
        e32p_mqtt_subscribe(s_mqtt, s_topic_cmd, 1, NULL);
        e32p_mqtt_publish(s_mqtt, s_topic_evt, "online", 0, 1, true, NULL);
        break;
    case E32P_MQTT_EVENT_DATA:
        /* A retained command is replayed at every subscribe: a retained "reboot" would
           reboot the gateway on every connect. Only live commands count. */
        if (ev->retain) {
            ESP_LOGW(TAG, "retained cmd ignored (%d bytes)", ev->data_len);
            break;
        }
        handle_cmd(ev->data, ev->data_len);   /* only the cmd topic is subscribed */
        break;
    case E32P_MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "broker disconnected");
        break;
    default:
        break;
    }
}

/* --- local UI -------------------------------------------------------------- */

static void ui_update(bool cloud, float headline)
{
    led_set(cloud);                       /* solid = cloud connected */
    if (s_disp) {
        e32p_tm1637_show_int(s_disp, (int)headline, false, false);
    }
}

/* --- normal (provisioned) operation ---------------------------------------- */

static void run_gateway(void)
{
    ESP_LOGI(TAG, "=== e32p edge gateway '%s' ===", s_cfg.id);

    /* Confirm a freshly-OTA'd image so the bootloader won't roll it back. */
    if (e32p_ota_is_pending_verify()) {
        e32p_ota_mark_valid();
        ESP_LOGI(TAG, "OTA image confirmed good");
    }

    led_init();

    e32p_tm1637_config_t dcfg = { .clk_pin = CONFIG_GW_TM1637_CLK_GPIO,
                                  .dio_pin = CONFIG_GW_TM1637_DIO_GPIO, .brightness = 4 };
    if (e32p_tm1637_init(&dcfg, &s_disp) != ESP_OK) {
        s_disp = NULL;
        ESP_LOGW(TAG, "no TM1637 display — continuing without local UI");
    }

#if CONFIG_GW_SOURCE_J1939
    s_src = gw_source_j1939();
#else
    s_src = gw_source_sim();
#endif
    if (s_src->start() != ESP_OK) {
        ESP_LOGE(TAG, "field source '%s' failed to start — falling back to sim", s_src->kind);
        s_src = gw_source_sim();
        s_src->start();
    }
    ESP_LOGI(TAG, "field source: %s", s_src->kind);

    ESP_ERROR_CHECK(e32p_wifi_init());
    ESP_LOGI(TAG, "connecting Wi-Fi \"%s\"...", s_cfg.ssid);
    if (e32p_wifi_sta_connect(s_cfg.ssid, s_cfg.pass, 20000) != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi connect failed — rebooting to retry");
        vTaskDelay(pdMS_TO_TICKS(2000));
        esp_restart();
    }
    char ip[16] = { 0 };
    e32p_wifi_get_ip(ip, sizeof(ip));
    ESP_LOGI(TAG, "Wi-Fi up (IP %s)", ip);

    snprintf(s_topic_tel, sizeof(s_topic_tel), "gw/%s/telemetry", s_cfg.id);
    snprintf(s_topic_cmd, sizeof(s_topic_cmd), "gw/%s/cmd", s_cfg.id);
    snprintf(s_topic_evt, sizeof(s_topic_evt), "gw/%s/evt", s_cfg.id);

    e32p_mqtt_config_t mcfg = { .broker_uri = s_cfg.broker, .client_id = s_cfg.id, .cb = on_mqtt };
    ESP_ERROR_CHECK(e32p_mqtt_init(&mcfg, &s_mqtt));
    ESP_ERROR_CHECK(e32p_mqtt_start(s_mqtt));
    ESP_LOGI(TAG, "online: telemetry->%s cmd->%s (every %d s)",
             s_topic_tel, s_topic_cmd, (int)s_cfg.interval_s);

    e32p_wdt_init(10000, true);
    e32p_wdt_watch_current_task();

    char json[512];
    gw_sample_t samples[8];
    uint32_t last_pub = 0;

    for (;;) {
        e32p_wdt_feed();

        if (s_reboot) {
            ESP_LOGW(TAG, "reboot requested");
            vTaskDelay(pdMS_TO_TICKS(200));
            esp_restart();
        }
        if (s_identify) {
            s_identify = false;
            ESP_LOGI(TAG, "identify");
            for (int i = 0; i < 6; i++) {
                led_set(i & 1);
                vTaskDelay(pdMS_TO_TICKS(120));
            }
        }

        int n = s_src->poll(samples, 8);
        float headline = (n > 0) ? samples[0].value : 0.0f;
        bool cloud = e32p_mqtt_is_connected(s_mqtt);
        ui_update(cloud, headline);

        uint32_t now = (uint32_t)(e32p_sysinfo_uptime_ms() / 1000);
        if (cloud && n > 0 && (now - last_pub) >= (uint32_t)s_cfg.interval_s) {
            last_pub = now;
            int jl = build_telemetry(json, sizeof(json), samples, n);
            e32p_mqtt_publish(s_mqtt, s_topic_tel, json, jl, 0, false, NULL);
            ESP_LOGI(TAG, "TX %s (%d bytes, %d signals)", s_topic_tel, jl, n);
        }

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

/* --- entry ----------------------------------------------------------------- */

void app_main(void)
{
    ESP_ERROR_CHECK(e32p_nvs_init());
    gw_config_load(&s_cfg);

    /* Bench convenience: if nothing is provisioned yet but build-time fallback
       creds are set (menuconfig), use them so the cloud path can be tested
       without a phone. Real onboarding always goes through BLE. */
    if (!gw_config_is_provisioned(&s_cfg) && strlen(CONFIG_GW_FALLBACK_WIFI_SSID) > 0) {
        strncpy(s_cfg.ssid, CONFIG_GW_FALLBACK_WIFI_SSID, sizeof(s_cfg.ssid) - 1);
        strncpy(s_cfg.pass, CONFIG_GW_FALLBACK_WIFI_PASSWORD, sizeof(s_cfg.pass) - 1);
        strncpy(s_cfg.broker, CONFIG_GW_FALLBACK_BROKER_URI, sizeof(s_cfg.broker) - 1);
        ESP_LOGW(TAG, "using build-time fallback creds (not BLE-provisioned)");
    }

    if (!gw_config_is_provisioned(&s_cfg)) {
        ESP_LOGW(TAG, "unprovisioned — entering BLE provisioning");
        gw_provision_run(&s_cfg);   /* never returns (reboots on apply) */
        return;
    }

    run_gateway();
}
