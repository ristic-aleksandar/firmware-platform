/**
 * @file main.c
 * @brief 26_ota_http — pull a firmware image from an HTTP(S) server and update.
 *
 * On the first boot it connects to Wi-Fi, downloads the .bin at EXAMPLE_OTA_URL
 * into the spare OTA slot, and reboots into it. The updated image boots in
 * "pending verify", confirms itself with mark_valid(), and idles — the running
 * slot has flipped (ota_0 -> ota_1), proving the pull worked. A one-shot NVS flag
 * stops it from re-pulling in a loop; erase NVS (`idf.py -p COM5 erase-flash`) to
 * run the test again.
 *
 * Set Wi-Fi credentials and the URL via `idf.py menuconfig` -> "26_ota_http
 * example" (they are not hardcoded). Serve a .bin with `python -m http.server 8080`.
 *
 * !!! SECURITY — TESTING ONLY !!!
 * This demo pulls over plain HTTP, which requires CONFIG_ESP_HTTPS_OTA_ALLOW_HTTP=y
 * (set in sdkconfig.defaults). HTTP OTA is UNAUTHENTICATED and UNENCRYPTED: a
 * man-in-the-middle on the network can serve malicious firmware that the device
 * will flash and boot. It is fine on a trusted bench LAN, but MUST NOT ship.
 * For production: use an https:// URL with server-certificate verification, turn
 * CONFIG_ESP_HTTPS_OTA_ALLOW_HTTP off, and enable Secure Boot v2 so unsigned
 * images are rejected. See components/e32p_ota/README.md ("Security & production
 * hardening").
 */
#include "sdkconfig.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "e32p_wifi.h"
#include "e32p_ota.h"
#include "e32p_nvs.h"

#if CONFIG_E32P_BENCH_FORBIDDEN
#error "Example 26 is demo-only (plain-HTTP OTA demo); " \
       "this build is marked as a product (E32P_PRODUCT_BUILD, Secure Boot or Flash Encryption)"
#endif

static const char *TAG = "ota_http";

#define NS   "ota_http"
#define KEY  "pulled"

static void idle_forever(void)
{
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

static void print_status(void)
{
    e32p_ota_app_info_t info;
    e32p_ota_get_app_info(&info);
    ESP_LOGI(TAG, "running slot: %s   app: %s   ver: %s   built: %s %s",
             e32p_ota_running_label(), info.project, info.version, info.date, info.time);
}

void app_main(void)
{
    ESP_ERROR_CHECK(e32p_nvs_init());
    ESP_LOGI(TAG, "=== e32p_ota HTTP-pull demo ===");
    print_status();

    /* Confirm a freshly pulled image so the bootloader doesn't roll it back. */
    if (e32p_ota_is_pending_verify()) {
        ESP_LOGI(TAG, "this image is pending-verify -> marking valid (rollback cancelled)");
        e32p_ota_mark_valid();
    }

    int32_t pulled = 0;
    if (e32p_nvs_is_not_found(e32p_nvs_read_i32(NS, KEY, &pulled))) {
        pulled = 0;
    }
    if (pulled) {
        ESP_LOGI(TAG, "already pulled once; running the downloaded firmware. Idling.");
        ESP_LOGI(TAG, "(run `idf.py -p COM5 erase-flash` then reflash to pull again)");
        idle_forever();
    }

    if (strlen(CONFIG_EXAMPLE_WIFI_SSID) == 0 || strlen(CONFIG_EXAMPLE_OTA_URL) == 0) {
        ESP_LOGE(TAG, "set Wi-Fi SSID + OTA URL via `idf.py menuconfig` -> \"26_ota_http example\"");
        idle_forever();
    }

    ESP_ERROR_CHECK(e32p_wifi_init());
    ESP_LOGI(TAG, "connecting to Wi-Fi \"%s\"...", CONFIG_EXAMPLE_WIFI_SSID);
    if (e32p_wifi_sta_connect(CONFIG_EXAMPLE_WIFI_SSID, CONFIG_EXAMPLE_WIFI_PASSWORD, 20000) != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi connect failed");
        idle_forever();
    }
    char ip[16] = { 0 };
    e32p_wifi_get_ip(ip, sizeof(ip));
    ESP_LOGI(TAG, "Wi-Fi up (IP %s); pulling firmware from %s", ip, CONFIG_EXAMPLE_OTA_URL);

    /* TESTING ONLY over http:// (no server auth). Production: https:// + a verified
     * certificate + Secure Boot. See the file header and e32p_ota's README. */
    esp_err_t err = e32p_ota_update_from_url(CONFIG_EXAMPLE_OTA_URL, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA failed: %s (boot slot unchanged, no harm done)", esp_err_to_name(err));
        idle_forever();
    }

    e32p_nvs_write_i32(NS, KEY, 1);
    ESP_LOGI(TAG, "downloaded + staged; rebooting into the new image...");
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
}
