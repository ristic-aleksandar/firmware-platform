#pragma once
/**
 * @file e32p_wifi.h
 * @brief Thin facade over esp_wifi: bring up Station or SoftAP with a few calls.
 *        Handles netif, the default event loop, NVS, and connection state
 *        internally. The public API is free of esp_wifi types.
 */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "e32p_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the Wi-Fi stack (netif + default event loop + NVS + driver).
 *        Idempotent; pulls up its dependencies (e32p_nvs, e32p_event) itself.
 * @return ESP_OK or a driver error.
 */
esp_err_t e32p_wifi_init(void);

/** @brief Stop and tear down the Wi-Fi stack. */
esp_err_t e32p_wifi_deinit(void);

/* ---- Station (connect to an AP) ---- */

/**
 * @brief Connect to an AP and block until an IP is obtained or @p timeout_ms
 *        elapses. Auto-reconnects on later drops.
 * @param[in] ssid       Network name (must not be NULL).
 * @param[in] password   Passphrase, or NULL/"" for an open network.
 * @param[in] timeout_ms Max time to wait for an IP.
 * @return ESP_OK, ESP_ERR_TIMEOUT, ESP_FAIL (auth/no-AP after retries),
 *         ESP_ERR_INVALID_ARG, or a driver error.
 */
esp_err_t e32p_wifi_sta_connect(const char *ssid, const char *password, uint32_t timeout_ms);

/** @brief Disconnect and stop auto-reconnect. */
esp_err_t e32p_wifi_sta_disconnect(void);

/** @brief True once connected with an IP. */
bool e32p_wifi_is_connected(void);

/**
 * @brief Copy the current IPv4 as a dotted string.
 * @param[out] out_ip  Buffer (>= 16 bytes).
 * @param[in]  len     Buffer size.
 * @return ESP_OK, ESP_ERR_INVALID_ARG, or ESP_ERR_INVALID_STATE (not connected).
 */
esp_err_t e32p_wifi_get_ip(char *out_ip, size_t len);

/**
 * @brief Current AP signal strength in dBm.
 * @return ESP_OK, ESP_ERR_INVALID_ARG, ESP_ERR_INVALID_STATE, or a driver error.
 */
esp_err_t e32p_wifi_get_rssi(int8_t *out_rssi);

/* ---- SoftAP (be an AP) ---- */

/**
 * @brief Start a SoftAP. NULL/empty password → open network.
 * @param[in] ssid      Network name (must not be NULL).
 * @param[in] password  Passphrase or NULL/"".
 * @param[in] channel   1..13.
 * @param[in] max_conn  Max stations (1..10).
 * @return ESP_OK, ESP_ERR_INVALID_ARG, or a driver error.
 */
esp_err_t e32p_wifi_ap_start(const char *ssid, const char *password, uint8_t channel, uint8_t max_conn);
esp_err_t e32p_wifi_ap_stop(void);

#ifdef __cplusplus
}
#endif
