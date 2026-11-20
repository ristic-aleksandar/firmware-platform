/**
 * @file main.c
 * @brief 25_ota — a self-contained OTA demo that needs no server.
 *
 * Each boot the app clones its own running image into the *other* OTA slot, sets
 * it as the boot partition, and reboots. On the next boot it runs from that slot
 * in "pending verify" state and calls mark_valid() to confirm it (otherwise the
 * bootloader would roll back). It does this twice — ota_0 -> ota_1 -> ota_0 — so
 * you can watch the running slot flip and the boot counter persist, exercising
 * the whole esp_ota begin/write/end + set-boot + rollback path with no network.
 *
 * Wiring: none. Just flash and watch the serial log.
 */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "e32p_ota.h"
#include "e32p_nvs.h"

static const char *TAG = "ota_demo";

#define NS        "ota_demo"
#define KEY_HOPS  "hops"
#define MAX_HOPS  2

static uint8_t s_buf[4096];   /* copy buffer (static: keep it off the task stack) */

static void print_status(void)
{
    e32p_ota_app_info_t info;
    e32p_ota_get_app_info(&info);
    size_t size = 0;
    e32p_ota_running_image_size(&size);

    ESP_LOGI(TAG, "running slot: %s   (next update -> %s)",
             e32p_ota_running_label(), e32p_ota_next_label());
    ESP_LOGI(TAG, "app: %s  ver: %s  built: %s %s", info.project, info.version, info.date, info.time);
    ESP_LOGI(TAG, "image: %u bytes   pending-verify: %d   rollback-possible: %d",
             (unsigned)size, e32p_ota_is_pending_verify(), e32p_ota_rollback_possible());
}

/* Copy the running image into the next slot and stage it for boot. */
static esp_err_t self_clone(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    size_t image_size = 0;
    esp_err_t err = e32p_ota_running_image_size(&image_size);
    if (err != ESP_OK) {
        return err;
    }

    e32p_ota_handle_t h = NULL;
    err = e32p_ota_begin(&h);
    if (err != ESP_OK) {
        return err;
    }

    size_t off = 0;
    int last_decile = -1;
    while (off < image_size) {
        size_t chunk = (image_size - off > sizeof(s_buf)) ? sizeof(s_buf) : (image_size - off);
        err = esp_partition_read(running, off, s_buf, chunk);
        if (err != ESP_OK) {
            break;
        }
        err = e32p_ota_write(h, s_buf, chunk);
        if (err != ESP_OK) {
            break;
        }
        off += chunk;
        int decile = (int)(off * 10 / image_size);
        if (decile != last_decile) {
            ESP_LOGI(TAG, "  copying... %d%%", decile * 10);
            last_decile = decile;
        }
    }
    if (err != ESP_OK) {
        e32p_ota_abort(h);
        return err;
    }
    return e32p_ota_end(h);   /* validate the image + set boot partition */
}

void app_main(void)
{
    ESP_ERROR_CHECK(e32p_nvs_init());
    ESP_LOGI(TAG, "=== e32p_ota self-OTA demo (no server needed) ===");
    print_status();

    /* If we just booted a freshly-cloned image, confirm it so it isn't rolled back. */
    if (e32p_ota_is_pending_verify()) {
        ESP_LOGI(TAG, "this image is pending-verify -> marking valid (rollback cancelled)");
        e32p_ota_mark_valid();
    }

    int32_t hops = 0;
    esp_err_t r = e32p_nvs_read_i32(NS, KEY_HOPS, &hops);
    if (e32p_nvs_is_not_found(r)) {
        hops = 0;
    }

    if (hops >= MAX_HOPS) {
        ESP_LOGI(TAG, "demo complete after %d hop(s); now running from %s. Idling.",
                 (int)hops, e32p_ota_running_label());
        for (;;) {
            vTaskDelay(pdMS_TO_TICKS(5000));
        }
    }

    ESP_LOGI(TAG, "waiting 3 s, then cloning myself into %s ...", e32p_ota_next_label());
    vTaskDelay(pdMS_TO_TICKS(3000));

    esp_err_t err = self_clone();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "self-OTA failed: %s (boot slot unchanged, no harm done)", esp_err_to_name(err));
        for (;;) {
            vTaskDelay(pdMS_TO_TICKS(5000));
        }
    }

    e32p_nvs_write_i32(NS, KEY_HOPS, hops + 1);
    ESP_LOGI(TAG, "update staged; rebooting into the other slot (hop %d of %d)...",
             (int)hops + 1, MAX_HOPS);
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
}
