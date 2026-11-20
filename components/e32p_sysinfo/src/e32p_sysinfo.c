/**
 * @file e32p_sysinfo.c
 * @brief e32p_sysinfo implementation — read-only system introspection.
 */
#include <string.h>
#include "esp_log.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_mac.h"
#include "esp_app_desc.h"
#include "esp_timer.h"
#include "e32p_sysinfo.h"

static const char *TAG = "e32p_sysinfo";

static const char *chip_model_str(esp_chip_model_t model)
{
    switch (model) {
    case CHIP_ESP32:   return "ESP32";
    case CHIP_ESP32S2: return "ESP32-S2";
    case CHIP_ESP32S3: return "ESP32-S3";
    case CHIP_ESP32C3: return "ESP32-C3";
    case CHIP_ESP32C6: return "ESP32-C6";
    case CHIP_ESP32H2: return "ESP32-H2";
    default:           return "unknown";
    }
}

e32p_reset_reason_t e32p_sysinfo_reset_reason(void)
{
    switch (esp_reset_reason()) {
    case ESP_RST_POWERON:   return E32P_RESET_POWERON;
    case ESP_RST_SW:        return E32P_RESET_SW;
    case ESP_RST_PANIC:     return E32P_RESET_PANIC;
    case ESP_RST_INT_WDT:   return E32P_RESET_INT_WDT;
    case ESP_RST_TASK_WDT:  return E32P_RESET_TASK_WDT;
    case ESP_RST_BROWNOUT:  return E32P_RESET_BROWNOUT;
    case ESP_RST_DEEPSLEEP: return E32P_RESET_DEEPSLEEP;
    default:                return E32P_RESET_OTHER;
    }
}

const char *e32p_sysinfo_reset_reason_str(void)
{
    switch (e32p_sysinfo_reset_reason()) {
    case E32P_RESET_POWERON:   return "power-on";
    case E32P_RESET_SW:        return "software";
    case E32P_RESET_PANIC:     return "panic";
    case E32P_RESET_INT_WDT:   return "interrupt watchdog";
    case E32P_RESET_TASK_WDT:  return "task watchdog";
    case E32P_RESET_BROWNOUT:  return "brownout";
    case E32P_RESET_DEEPSLEEP: return "deep-sleep wake";
    case E32P_RESET_OTHER:     return "other";
    default:                   return "unknown";
    }
}

esp_err_t e32p_sysinfo_get(e32p_sysinfo_t *out)
{
    E32P_CHECK_ARG(out != NULL, TAG);
    memset(out, 0, sizeof(*out));

    esp_chip_info_t chip;
    esp_chip_info(&chip);
    strncpy(out->chip_model, chip_model_str(chip.model), sizeof(out->chip_model) - 1);
    out->cores    = chip.cores;
    out->revision = chip.revision;

    uint32_t flash_size = 0;
    if (esp_flash_get_size(NULL, &flash_size) == ESP_OK) {
        out->flash_size_bytes = flash_size;
    }

    esp_efuse_mac_get_default(out->mac);
    strncpy(out->idf_version, esp_get_idf_version(), sizeof(out->idf_version) - 1);

    const esp_app_desc_t *app = esp_app_get_description();
    if (app != NULL) {
        strncpy(out->app_name, app->project_name, sizeof(out->app_name) - 1);
        strncpy(out->app_version, app->version, sizeof(out->app_version) - 1);
    }
    return ESP_OK;
}

uint64_t e32p_sysinfo_uptime_ms(void)
{
    return (uint64_t)esp_timer_get_time() / 1000ULL;
}

uint32_t e32p_sysinfo_free_heap(void)
{
    return esp_get_free_heap_size();
}

uint32_t e32p_sysinfo_min_free_heap(void)
{
    return esp_get_minimum_free_heap_size();
}

void e32p_sysinfo_log(void)
{
    e32p_sysinfo_t info;
    if (e32p_sysinfo_get(&info) != ESP_OK) {
        return;
    }
    ESP_LOGI(TAG, "chip=%s cores=%d rev=v%d.%d flash=%uMB", info.chip_model, info.cores,
             info.revision / 100, info.revision % 100,
             (unsigned)(info.flash_size_bytes / (1024U * 1024U)));
    ESP_LOGI(TAG, "MAC=%02x:%02x:%02x:%02x:%02x:%02x",
             (unsigned)info.mac[0], (unsigned)info.mac[1], (unsigned)info.mac[2],
             (unsigned)info.mac[3], (unsigned)info.mac[4], (unsigned)info.mac[5]);
    ESP_LOGI(TAG, "app=%s ver=%s idf=%s", info.app_name, info.app_version, info.idf_version);
    ESP_LOGI(TAG, "reset=%s free_heap=%u min_free=%u", e32p_sysinfo_reset_reason_str(),
             (unsigned)e32p_sysinfo_free_heap(), (unsigned)e32p_sysinfo_min_free_heap());
}
