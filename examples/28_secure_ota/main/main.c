/**
 * @file main.c
 * @brief 28_secure_ota — signed OTA in action, proving BOTH protection layers.
 *
 * Built with signed-app verification (CONFIG_SECURE_SIGNED_ON_UPDATE_NO_SECURE_BOOT):
 * every OTA image must have intact contents AND a valid signature made with our
 * key, or esp_ota_end() refuses it. No eFuses are burned (fully reversible); see
 * docs/SECURITY_HARDENING.md for the eFuse-based Secure Boot v2 / Flash Encryption.
 *
 * Two self-contained tests (no server, no wiring), both non-destructive:
 *   1. INTEGRITY   — clone this firmware with one byte flipped in the app body.
 *                    The image checksum fails -> rejected.
 *   2. AUTHENTICITY — clone the app image but omit its appended signature block.
 *                    Contents are valid (checksum passes) but there's no valid
 *                    signature -> rejected by the signature check specifically.
 * A failed update never changes the boot slot; a safety net also reverts it if a
 * test somehow passes, so the device always stays on its trusted firmware.
 */
#include "sdkconfig.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "e32p_ota.h"

#if CONFIG_E32P_BENCH_FORBIDDEN
#error "Example 28 is demo-only (throwaway signing key); " \
       "this build is marked as a product (E32P_PRODUCT_BUILD, Secure Boot or Flash Encryption)"
#endif

static const char *TAG = "secure_ota";

static uint8_t s_buf[4096];

/* Clone `copy_len` bytes of `running` into the next slot, flipping one byte at
   `tamper_off` (or -1 for none), and return the verification verdict. On an
   unexpected success it reverts the boot slot so the trusted image keeps booting. */
static esp_err_t try_install(const esp_partition_t *running, size_t copy_len, long tamper_off)
{
    e32p_ota_handle_t h = NULL;
    if (e32p_ota_begin(&h) != ESP_OK) {
        return ESP_FAIL;
    }
    size_t off = 0;
    esp_err_t err = ESP_OK;
    while (off < copy_len) {
        size_t chunk = (copy_len - off > sizeof(s_buf)) ? sizeof(s_buf) : (copy_len - off);
        if (esp_partition_read(running, off, s_buf, chunk) != ESP_OK) {
            err = ESP_FAIL;
            break;
        }
        if (tamper_off >= 0 && (size_t)tamper_off >= off && (size_t)tamper_off < off + chunk) {
            s_buf[(size_t)tamper_off - off] ^= 0xFF;
        }
        err = e32p_ota_write(h, s_buf, chunk);
        if (err != ESP_OK) {
            break;
        }
        off += chunk;
    }
    if (err != ESP_OK) {
        e32p_ota_abort(h);
        return err;
    }
    esp_err_t verdict = e32p_ota_end(h);   /* validates; sets boot only on success */
    if (verdict == ESP_OK) {
        esp_ota_set_boot_partition(running); /* unexpected accept -> stay on trusted image */
    }
    return verdict;
}

static void report(const char *layer, esp_err_t verdict)
{
    if (verdict != ESP_OK) {
        ESP_LOGI(TAG, "%s: REJECTED (%s) — image refused, boot slot unchanged", layer,
                 esp_err_to_name(verdict));
    } else {
        ESP_LOGE(TAG, "%s: UNEXPECTED — image ACCEPTED (verification not active?)", layer);
    }
}

void app_main(void)
{
    e32p_ota_app_info_t info;
    e32p_ota_get_app_info(&info);
    ESP_LOGI(TAG, "=== signed OTA demo ===");
    ESP_LOGI(TAG, "running slot: %s   app: %s   ver: %s", e32p_ota_running_label(), info.project, info.version);
    ESP_LOGI(TAG, "signed-image verification is ENABLED");

    const esp_partition_t *running = esp_ota_get_running_partition();
    size_t image_size = 0;
    if (e32p_ota_running_image_size(&image_size) != ESP_OK || image_size == 0) {
        ESP_LOGE(TAG, "could not read running image size");
    } else {
        /* 1. Integrity: flip a byte in the app body -> checksum fails. */
        ESP_LOGI(TAG, "[1/2] integrity: installing a copy with one flipped byte...");
        report("integrity", try_install(running, image_size, (long)(image_size / 2)));

        /* 2. Authenticity: clone the app WITHOUT its appended signature block ->
              contents valid, but no valid signature -> signature check rejects it. */
        ESP_LOGI(TAG, "[2/2] authenticity: installing a copy with the signature omitted...");
        report("authenticity", try_install(running, image_size, -1));
    }

    ESP_LOGI(TAG, "done — device stays on its trusted firmware in %s", e32p_ota_running_label());
    ESP_LOGI(TAG, "see docs/SECURITY_HARDENING.md for the full production hardening process");
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
