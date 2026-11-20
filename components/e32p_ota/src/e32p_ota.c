/**
 * @file e32p_ota.c
 * @brief e32p_ota implementation — esp_ota_ops queries/rollback/streaming plus an
 *        esp_https_ota convenience.
 */
#include "sdkconfig.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_https_ota.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "e32p_ota.h"

/* A product build must not allow plain-HTTP OTA or turn off the TLS server checks.
 * CONFIG_E32P_BENCH_FORBIDDEN comes from e32p_core (product marker, Secure Boot or
 * Flash Encryption). The three symbols are in the pinned ESP-IDF v5.3.1 Kconfig. */
#if CONFIG_E32P_BENCH_FORBIDDEN && \
    (CONFIG_ESP_HTTPS_OTA_ALLOW_HTTP || CONFIG_ESP_TLS_INSECURE || \
     CONFIG_ESP_TLS_SKIP_SERVER_CERT_VERIFY)
#error "e32p_ota: plain-HTTP OTA and disabled TLS server checks are not allowed in a product build."
#endif

/* A product build must also check image signatures. The CA bundle proves the server, not
 * the image. CONFIG_SECURE_SIGNED_ON_UPDATE is set by Secure Boot or by signed-on-update
 * without Secure Boot (pinned ESP-IDF v5.3.1 bootloader Kconfig). */
#if CONFIG_E32P_BENCH_FORBIDDEN && !CONFIG_SECURE_SIGNED_ON_UPDATE
#error "e32p_ota: a product build must verify image signatures (CONFIG_SECURE_SIGNED_ON_UPDATE)."
#endif

/* The check above is not enough: CONFIG_SECURE_SIGNED_ON_UPDATE is also set by the bench
 * option "signed apps, no Secure Boot", which trusts the bench ECDSA keys. On the ESP32
 * rev v3 and later, Secure Boot v2 uses RSA, so the bench ECDSA keys cannot sign a product
 * image. A product must therefore turn Secure Boot on. */
#if CONFIG_E32P_PRODUCT_BUILD && !CONFIG_SECURE_BOOT
#error "e32p_ota: a product build must enable Secure Boot (CONFIG_SECURE_BOOT); " \
       "signed apps without Secure Boot trust bench keys."
#endif

static const char *TAG = "e32p_ota";

struct e32p_ota_ctx {
    esp_ota_handle_t      handle;
    const esp_partition_t *part;
};

/* ---- Queries -------------------------------------------------------------- */

const char *e32p_ota_running_label(void)
{
    const esp_partition_t *p = esp_ota_get_running_partition();
    return p ? p->label : NULL;
}

const char *e32p_ota_next_label(void)
{
    const esp_partition_t *p = esp_ota_get_next_update_partition(NULL);
    return p ? p->label : NULL;
}

esp_err_t e32p_ota_get_app_info(e32p_ota_app_info_t *out)
{
    E32P_CHECK_ARG(out != NULL, TAG);
    const esp_app_desc_t *d = esp_app_get_description();
    if (d == NULL) {
        return ESP_FAIL;
    }
    snprintf(out->project, sizeof(out->project), "%s", d->project_name);
    snprintf(out->version, sizeof(out->version), "%s", d->version);
    snprintf(out->idf_version, sizeof(out->idf_version), "%s", d->idf_ver);
    snprintf(out->date, sizeof(out->date), "%s", d->date);
    snprintf(out->time, sizeof(out->time), "%s", d->time);
    memcpy(out->sha256, d->app_elf_sha256, sizeof(out->sha256));
    return ESP_OK;
}

esp_err_t e32p_ota_running_image_size(size_t *out_bytes)
{
    E32P_CHECK_ARG(out_bytes != NULL, TAG);
    const esp_partition_t *p = esp_ota_get_running_partition();
    if (p == NULL) {
        return ESP_FAIL;
    }
    esp_image_header_t hdr;
    if (esp_partition_read(p, 0, &hdr, sizeof(hdr)) != ESP_OK || hdr.magic != ESP_IMAGE_HEADER_MAGIC) {
        return ESP_FAIL;
    }
    size_t off = sizeof(esp_image_header_t);
    for (int i = 0; i < hdr.segment_count; i++) {
        esp_image_segment_header_t seg;
        if (esp_partition_read(p, off, &seg, sizeof(seg)) != ESP_OK) {
            return ESP_FAIL;
        }
        off += sizeof(seg) + seg.data_len;
    }
    off += 1;                              /* checksum byte */
    off = (off + 15u) & ~((size_t)15u);    /* 16-byte aligned */
    if (hdr.hash_appended) {
        off += 32;                         /* appended SHA-256 */
    }
    *out_bytes = off;
    return ESP_OK;
}

bool e32p_ota_is_pending_verify(void)
{
    const esp_partition_t *p = esp_ota_get_running_partition();
    esp_ota_img_states_t st;
    if (p != NULL && esp_ota_get_state_partition(p, &st) == ESP_OK) {
        return st == ESP_OTA_IMG_PENDING_VERIFY;
    }
    return false;
}

bool e32p_ota_rollback_possible(void)
{
    return esp_ota_check_rollback_is_possible();
}

/* ---- Rollback control ----------------------------------------------------- */

esp_err_t e32p_ota_mark_valid(void)
{
    return esp_ota_mark_app_valid_cancel_rollback();
}

esp_err_t e32p_ota_rollback_and_reboot(void)
{
    return esp_ota_mark_app_invalid_rollback_and_reboot();   /* no return on success */
}

/* ---- Streaming update ----------------------------------------------------- */

/**
 * @brief Cancel a staged image that has not booted yet, before its slot is erased.
 *
 * SECURITY: an image that end() (or a URL pull) staged but that has not booted yet is
 * the boot target. Erasing its slot while otadata still names it would leave the next
 * reset to boot whatever is written there (the bootloader checks the hash, not the
 * signature, unless Secure Boot is on). So a new update first points boot back at the
 * running partition. If that fails, the caller must not erase anything.
 *
 * With rollback on, esp_ota_set_boot_partition() writes the running slot's otadata entry
 * as NEW, which the bootloader would turn into pending-verify. So the cancel confirms the
 * entry (VALID) right after. This is safe: the cancel only runs when boot names the other
 * slot, and the running image is not pending verify then, because esp_ota_begin() refuses
 * to stage anything while it is. Anti-rollback is off in every config of this repo.
 *
 * @param[in] target  The slot the new update is about to write (NULL: nothing to cancel).
 * @return ESP_OK if @p target is NULL, boot does not name @p target, or boot was moved
 *         back to the running partition; ESP_ERR_NOT_FOUND if the boot partition cannot
 *         be read; otherwise the error from esp_ota_set_boot_partition() or
 *         esp_ota_mark_app_valid_cancel_rollback().
 */
static esp_err_t cancel_staged_update(const esp_partition_t *target)
{
    if (target == NULL) {
        return ESP_OK;
    }
    const esp_partition_t *boot = esp_ota_get_boot_partition();
    if (boot == NULL) {
        ESP_LOGE(TAG, "cannot read the boot partition; staged image state unknown");
        return ESP_ERR_NOT_FOUND;
    }
    if (boot != target) {
        return ESP_OK;
    }
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_err_t err = esp_ota_set_boot_partition(running);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "cannot cancel the staged image in %s: %s", target->label,
                 esp_err_to_name(err));
        return err;
    }
#ifdef CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
    /* A factory app has no otadata entry (set_boot erased otadata) and nothing to confirm. */
    if (running->subtype != ESP_PARTITION_SUBTYPE_APP_FACTORY) {
        err = esp_ota_mark_app_valid_cancel_rollback();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "cannot keep the running image valid after the cancel: %s",
                     esp_err_to_name(err));
            return err;
        }
    }
#endif
    ESP_LOGW(TAG, "staged image in %s cancelled: boot is back on the running slot",
             target->label);
    return ESP_OK;
}

esp_err_t e32p_ota_begin(e32p_ota_handle_t *out)
{
    E32P_CHECK_ARG(out != NULL, TAG);
    const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);
    if (next == NULL) {
        ESP_LOGE(TAG, "no OTA slot available (need an OTA partition table)");
        return ESP_ERR_NOT_FOUND;
    }

    E32P_RETURN_ON_ERR(cancel_staged_update(next), TAG);

    esp_ota_handle_t h = 0;
    E32P_RETURN_ON_ERR(esp_ota_begin(next, OTA_SIZE_UNKNOWN, &h), TAG);

    struct e32p_ota_ctx *ctx = calloc(1, sizeof(*ctx));
    if (ctx == NULL) {
        esp_ota_abort(h);
        return ESP_ERR_NO_MEM;
    }
    ctx->handle = h;
    ctx->part = next;
    ESP_LOGI(TAG, "ota begin -> %s", next->label);
    *out = ctx;
    return ESP_OK;
}

esp_err_t e32p_ota_write(e32p_ota_handle_t h, const void *data, size_t len)
{
    E32P_CHECK_ARG(h != NULL, TAG);
    E32P_CHECK_ARG(data != NULL, TAG);
    return esp_ota_write(h->handle, data, len);
}

esp_err_t e32p_ota_end(e32p_ota_handle_t h)
{
    E32P_CHECK_ARG(h != NULL, TAG);
    esp_err_t err = esp_ota_end(h->handle);        /* validates the image */
    if (err == ESP_OK) {
        err = esp_ota_set_boot_partition(h->part);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "ota staged: next boot -> %s", h->part->label);
        }
    }
    free(h);
    return err;
}

esp_err_t e32p_ota_abort(e32p_ota_handle_t h)
{
    if (h == NULL) {
        return ESP_OK;
    }
    esp_ota_abort(h->handle);
    free(h);
    return ESP_OK;
}

/* ---- HTTP(S) pull --------------------------------------------------------- */

/* Plain http is accepted only when the IDF bench option is on. With it off, the
 * IDF itself does NOT refuse http:// (it checks that option only when no server
 * verification is configured, and we always configure the CA bundle), so this
 * module refuses it. */
/** @brief 1 if plain http is allowed (bench option on), else 0. */
#if CONFIG_ESP_HTTPS_OTA_ALLOW_HTTP
#define E32P_OTA_ALLOW_PLAIN 1
#else
#define E32P_OTA_ALLOW_PLAIN 0
#endif

/**
 * @brief True if @p url starts with "https://" (scheme compared without case).
 *
 * @param[in] url  NUL-terminated URL; must not be NULL.
 * @return true if the URL starts with "https://", false otherwise.
 */
static bool url_is_https(const char *url)
{
    return strncasecmp(url, "https://", 8) == 0;
}

/**
 * @brief HTTP client event handler: note every connection that is not TLS.
 *
 * Called by esp_http_client each time it connects, including the reconnect after a
 * redirect, so a redirect from https to http is seen here. @p evt->user_data is a
 * bool that is set when a plain connection is made.
 */
static esp_err_t note_plain_connection(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_CONNECTED && evt->user_data != NULL &&
        esp_http_client_get_transport_type(evt->client) != HTTP_TRANSPORT_OVER_SSL) {
        *(bool *)evt->user_data = true;
    }
    return ESP_OK;
}

/**
 * @brief Run the esp_https_ota begin / perform / finish sequence.
 *
 * Same steps and return codes as esp_https_ota() in ESP-IDF v5.3.1, with one added
 * check: when plain http is not allowed and any connection (the first one or one
 * made after a redirect) was not TLS, the update is aborted before the first
 * esp_https_ota_perform(). Flash is first written inside perform, so nothing has
 * been written at that point.
 *
 * @param[in] ota  OTA configuration (its http_config carries the event handler).
 * @param[in] plain_seen  Flag set by note_plain_connection().
 * @return ESP_OK; ESP_ERR_INVALID_RESPONSE if a non-TLS connection was refused;
 *         otherwise the esp_https_ota error.
 */
static esp_err_t pull_image(const esp_https_ota_config_t *ota, const bool *plain_seen)
{
    esp_https_ota_handle_t h = NULL;
    esp_err_t err = esp_https_ota_begin(ota, &h);
    if (h == NULL) {
        ESP_LOGE(TAG, "ota begin failed: %s", esp_err_to_name(err));
        return ESP_FAIL;                    /* as esp_https_ota() */
    }
    /* One check after begin() is enough: perform() reconnects only for partial
     * downloads, which this module never enables. */
    if (!E32P_OTA_ALLOW_PLAIN && *plain_seen) {
        ESP_LOGE(TAG, "refused: the download used a connection without TLS (redirect to http?)");
        esp_https_ota_abort(h);
        return ESP_ERR_INVALID_RESPONSE;
    }
    do {
        err = esp_https_ota_perform(h);
    } while (err == ESP_ERR_HTTPS_OTA_IN_PROGRESS);
    if (err != ESP_OK) {
        esp_https_ota_abort(h);
        return err;
    }
    return esp_https_ota_finish(h);
}

esp_err_t e32p_ota_update_from_url(const char *url, uint32_t timeout_ms)
{
    E32P_CHECK_ARG(url != NULL, TAG);

    /* The pull (TLS included) runs on the caller's stack; see README "Caller stack".
       Free stack so far, in bytes on ESP-IDF (StackType_t is uint8_t). Log only. */
    ESP_LOGD(TAG, "caller stack high-water mark: %u bytes free",
             (unsigned)uxTaskGetStackHighWaterMark(NULL));

    /* SECURITY: the server is checked against the ESP-IDF CA bundle, so an https://
     * URL only works for a server whose certificate chains to a bundled root CA
     * (needs CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=y). If the check fails, no image is
     * written.
     *   - http:// has no encryption and no server check. It is refused here,
     *     before any network call, and a redirect from https to http is refused
     *     before anything is written. It works only with
     *     CONFIG_ESP_HTTPS_OTA_ALLOW_HTTP=y, which is for bench use. The build-time
     *     guard at the top of this file refuses that setting in a product build.
     *   - The CA bundle proves who the server is, not that the image is genuine.
     *     For that, sign the images (see docs/SECURITY_HARDENING.md). */
    if (!E32P_OTA_ALLOW_PLAIN && !url_is_https(url)) {
        ESP_LOGE(TAG, "refused: the URL must start with https:// (plain http is off)");
        return ESP_ERR_INVALID_ARG;
    }

    /* esp_https_ota_begin() picks esp_ota_get_next_update_partition(NULL) and perform()
     * erases it. Cancel a staged image in that slot first; on failure nothing is
     * downloaded or erased. With no OTA slot (NULL) this is a no-op and the pull
     * fails in esp_https_ota_begin() as before. */
    E32P_RETURN_ON_ERR(cancel_staged_update(esp_ota_get_next_update_partition(NULL)), TAG);

    bool plain_seen = false;
    esp_http_client_config_t http = {
        .url = url,
        .timeout_ms = timeout_ms ? (int)timeout_ms : 30000,
        .keep_alive_enable = true,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler = note_plain_connection,
        .user_data = &plain_seen,
    };
    esp_https_ota_config_t ota = { .http_config = &http };

    ESP_LOGI(TAG, "pulling firmware from %s", url);
    esp_err_t err = pull_image(&ota, &plain_seen);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "update downloaded and staged; reboot to apply");
    } else {
        ESP_LOGE(TAG, "ota from url failed: %s", esp_err_to_name(err));
    }
    return err;
}
