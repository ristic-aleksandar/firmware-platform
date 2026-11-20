/**
 * @file gw_provision.c
 * @brief BLE-GATT provisioning (see gw_provision.h).
 *
 * Service 0xFF00 with one write characteristic per field plus an "apply"
 * trigger and a status characteristic:
 *
 *   0xFF01  ssid    (write)
 *   0xFF02  pass    (write)
 *   0xFF03  broker  (write)   e.g. "mqtt://192.168.1.10:1883"
 *   0xFF04  id      (write)   gateway id, e.g. "gw-01"
 *   0xFF05  apply   (write)   any value -> save to NVS + reboot
 *   0xFF06  status  (read/notify)  "waiting" / "saved" / a hint
 *
 * Just-Works pairing/bonding is handled inside e32p_ble; here we only stage the
 * written fields and commit on apply.
 */
#include "gw_provision.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "e32p_nvs.h"
#include "e32p_ble.h"

static const char *TAG = "gw_prov";

enum { CH_SSID = 0, CH_PASS, CH_BROKER, CH_ID, CH_APPLY, CH_STATUS };

static gw_config_t s_stage;   /* accumulates writes until "apply" */

static void set_status(const char *msg)
{
    e32p_ble_set_value(CH_STATUS, msg, (uint16_t)strlen(msg));
    e32p_ble_notify(CH_STATUS, msg, (uint16_t)strlen(msg));   /* INVALID_STATE if no sub — fine */
}

/* Copy a GATT write into a fixed field, always NUL-terminating. */
static void copy_field(char *dst, size_t cap, const uint8_t *data, uint16_t len)
{
    if (len >= cap) {
        len = (uint16_t)(cap - 1);
    }
    memcpy(dst, data, len);
    dst[len] = '\0';
}

static void on_write(uint16_t idx, const uint8_t *data, uint16_t len, void *user)
{
    (void)user;
    switch (idx) {
    case CH_SSID:
        copy_field(s_stage.ssid, sizeof(s_stage.ssid), data, len);
        ESP_LOGI(TAG, "ssid set (%u bytes)", len);
        break;
    case CH_PASS:
        copy_field(s_stage.pass, sizeof(s_stage.pass), data, len);
        ESP_LOGI(TAG, "pass set (%u bytes)", len);
        break;
    case CH_BROKER:
        copy_field(s_stage.broker, sizeof(s_stage.broker), data, len);
        ESP_LOGI(TAG, "broker set: %s", s_stage.broker);
        break;
    case CH_ID:
        copy_field(s_stage.id, sizeof(s_stage.id), data, len);
        ESP_LOGI(TAG, "id set: %s", s_stage.id);
        break;
    case CH_APPLY:
        if (!gw_config_is_provisioned(&s_stage)) {
            ESP_LOGW(TAG, "apply ignored — need at least ssid + broker");
            set_status("need ssid+broker");
            return;
        }
        ESP_LOGI(TAG, "applying config -> save + reboot");
        set_status("saved");
        gw_config_save(&s_stage);
        vTaskDelay(pdMS_TO_TICKS(300));   /* let the notify/ack flush */
        esp_restart();
        break;
    default:
        break;
    }
}

void gw_provision_run(const gw_config_t *current)
{
    s_stage = *current;                       /* preserve id/interval defaults */
    if (s_stage.interval_s < 1) {
        s_stage.interval_s = 5;
    }

    /* Local (not static): the UUID helpers are function calls. */
    const e32p_ble_char_t chars[] = {
        { .uuid = e32p_ble_uuid16(0xFF01), .props = E32P_BLE_PROP_WRITE },
        { .uuid = e32p_ble_uuid16(0xFF02), .props = E32P_BLE_PROP_WRITE },
        { .uuid = e32p_ble_uuid16(0xFF03), .props = E32P_BLE_PROP_WRITE },
        { .uuid = e32p_ble_uuid16(0xFF04), .props = E32P_BLE_PROP_WRITE },
        { .uuid = e32p_ble_uuid16(0xFF05), .props = E32P_BLE_PROP_WRITE },
        { .uuid = e32p_ble_uuid16(0xFF06), .props = E32P_BLE_PROP_READ | E32P_BLE_PROP_NOTIFY },
    };
    e32p_ble_config_t cfg = {
        .device_name  = "e32p-gateway",
        .service_uuid = e32p_ble_uuid16(0xFF00),
        .chars = chars, .n_chars = 6,
        .on_write = on_write,
    };

    ESP_ERROR_CHECK(e32p_nvs_init());   /* BLE stores PHY cal + bond keys here */
    ESP_ERROR_CHECK(e32p_ble_init(&cfg));
    set_status("waiting");
    ESP_LOGI(TAG, "provisioning — connect to 'e32p-gateway' over BLE");
    ESP_LOGI(TAG, "write ssid->0xFF01 pass->0xFF02 broker->0xFF03 id->0xFF04, then apply->0xFF05");

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
