#pragma once
/**
 * @file e32p_ota.h
 * @brief Over-the-air firmware update: partition/version queries, rollback
 *        control, a streaming write API (update from any source), and an
 *        HTTP(S) pull convenience.
 *
 * Requires an OTA-capable partition table (otadata + ota_0/ota_1). The query and
 * rollback layer works offline and is unit-tested; the actual image download
 * needs a network + a server (see the 25_ota demo / README).
 *
 * The public header exposes no esp_ota types, so the module cherry-picks cleanly.
 */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "e32p_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Description of the running application. */
typedef struct {
    char    project[32];      /*!< Project name. */
    char    version[32];      /*!< App version string. */
    char    idf_version[32];  /*!< ESP-IDF version it was built with. */
    char    date[16];         /*!< Build date. */
    char    time[16];         /*!< Build time. */
    uint8_t sha256[32];       /*!< App ELF SHA-256. */
} e32p_ota_app_info_t;

/** @brief Opaque streaming-update handle. */
typedef struct e32p_ota_ctx *e32p_ota_handle_t;

/* ---- Queries (offline) ---------------------------------------------------- */

/** @brief Label of the partition the firmware is running from (e.g. "ota_0"),
 *         or NULL. */
const char *e32p_ota_running_label(void);

/** @brief Label of the slot the next update would be written to, or NULL if the
 *         partition table has no free OTA slot. */
const char *e32p_ota_next_label(void);

/**
 * @brief Fill @p out with the running app's description.
 * @return ESP_OK, ESP_ERR_INVALID_ARG, or ESP_FAIL.
 */
esp_err_t e32p_ota_get_app_info(e32p_ota_app_info_t *out);

/**
 * @brief Size in bytes of the running app image (header + segments + checksum).
 * @return ESP_OK, ESP_ERR_INVALID_ARG, or ESP_FAIL (bad image header).
 */
esp_err_t e32p_ota_running_image_size(size_t *out_bytes);

/** @brief True if the running image is awaiting confirmation (rollback armed). */
bool e32p_ota_is_pending_verify(void);

/** @brief True if a previous valid app exists to roll back to. */
bool e32p_ota_rollback_possible(void);

/* ---- Rollback control ----------------------------------------------------- */

/**
 * @brief Confirm the running image is good (cancel a pending rollback).
 *        Call this after a fresh update boots successfully.
 * @return ESP_OK or a driver error.
 */
esp_err_t e32p_ota_mark_valid(void);

/**
 * @brief Mark the running image bad and reboot into the previous app.
 *        Does not return on success.
 * @return An error if rollback is not possible.
 */
esp_err_t e32p_ota_rollback_and_reboot(void);

/* ---- Streaming update (from any source) ----------------------------------- */

/**
 * @brief Begin an update: select the next slot and erase it.
 *
 * Starting a new update cancels a staged image that has not booted yet. If an
 * earlier update ended with e32p_ota_end() and the device has not reset, the next
 * slot is still the boot target. begin() first points boot back at the running
 * slot, then erases. If it cannot do that, it returns the error and erases nothing.
 * With CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y the cancel also confirms the running
 * image, so it stays valid (it does not become pending-verify after the next reset).
 *
 * @param[out] out  Receives the handle.
 * @return ESP_OK; ESP_ERR_INVALID_ARG;
 *         ESP_ERR_NOT_FOUND (no OTA slot, or the boot partition cannot be read);
 *         ESP_ERR_NO_MEM; or a driver error (including the error from cancelling
 *         the staged image).
 */
esp_err_t e32p_ota_begin(e32p_ota_handle_t *out);

/** @brief Append image bytes (call repeatedly). */
esp_err_t e32p_ota_write(e32p_ota_handle_t h, const void *data, size_t len);

/**
 * @brief Finish: validate the written image and set it as the boot partition.
 *        Reboot afterwards to run it. Frees the handle.
 * @return ESP_OK, ESP_ERR_INVALID_ARG, ESP_ERR_OTA_VALIDATE_FAILED, or a driver error.
 */
esp_err_t e32p_ota_end(e32p_ota_handle_t h);

/** @brief Abort an in-progress update and free the handle (NULL is a no-op). */
esp_err_t e32p_ota_abort(e32p_ota_handle_t h);

/* ---- Convenience: HTTP(S) pull -------------------------------------------- */

/**
 * @brief Download a firmware image from @p url and stage it for the next boot.
 *        Reboot afterwards to run it. Needs a working netif.
 *        An https pull needs about 8 KB of stack in the calling task (TLS handshake).
 *        Like e32p_ota_begin(), it first cancels a staged image that has not booted yet
 *        (boot goes back to the running slot and the running image stays valid; see
 *        e32p_ota_begin()). If that fails, it returns the error with no network call.
 * @param[in] url         URL of the .bin. Must start with `https://` unless
 *                        `CONFIG_ESP_HTTPS_OTA_ALLOW_HTTP=y` (bench builds only).
 * @param[in] timeout_ms  Per-request timeout (0 -> 30 s default).
 * @return ESP_OK;
 *         ESP_ERR_INVALID_ARG if @p url is NULL, or does not start with `https://`
 *         while plain http is not allowed (nothing is sent on the network and nothing is
 *         written; a staged image is left in place);
 *         ESP_ERR_INVALID_RESPONSE if the server redirected the download and the
 *         redirect opens a connection without TLS while plain http is not
 *         allowed (a redirect that opens a connection without TLS is refused;
 *         no image bytes are written to flash; a staged image has already been cancelled);
 *         the error from cancelling a staged image (nothing is sent or erased), including
 *         ESP_ERR_NOT_FOUND if the boot partition cannot be read;
 *         otherwise an esp_https_ota error (ESP_FAIL if the download could not be
 *         started: connection, TLS check, HTTP status, or no OTA slot; a staged image has
 *         already been cancelled).
 *
 * @warning SECURITY
 *   - An `https://` URL is checked against the ESP-IDF CA bundle (needs
 *     `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=y`; without it this module does not
 *     compile). The server's certificate must chain to a bundled root CA, or the
 *     call fails and no image bytes are written.
 *   - An `http://` URL has no encryption and no server check, so a man-in-the-middle
 *     can serve malicious firmware. At run time it is refused, and so is a redirect
 *     from https to http, unless `CONFIG_ESP_HTTPS_OTA_ALLOW_HTTP=y`. That option is
 *     for bench use only, and this module refuses to build with it in a product build.
 *   - The full CA bundle trusts every public root CA. It proves the host named in
 *     the URL, not who chose the URL and not that the image is genuine. A product
 *     can build a bundle with its own CA only
 *     (`CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_NONE` plus
 *     `CONFIG_MBEDTLS_CUSTOM_CERTIFICATE_BUNDLE`).
 *   - Do not put credentials or tokens in the URL: it is logged, and a redirect to
 *     http:// is refused only after the plain request has been sent.
 *   - Certificate dates are checked only with `CONFIG_MBEDTLS_HAVE_TIME_DATE=y` and
 *     a clock that is set. Check that setting before relying on expiry.
 *   - To trust the image itself, sign the images. A product build
 *     (`CONFIG_E32P_BENCH_FORBIDDEN`) does not compile unless
 *     `CONFIG_SECURE_SIGNED_ON_UPDATE` is on. A build with the product marker
 *     (`CONFIG_E32P_PRODUCT_BUILD`) also does not compile unless Secure Boot
 *     (`CONFIG_SECURE_BOOT`) is on, so the bench "signed apps, no Secure Boot"
 *     keys cannot sign a product. See `docs/SECURITY_HARDENING.md`
 *     and this module's README ("Security & production hardening").
 */
esp_err_t e32p_ota_update_from_url(const char *url, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
