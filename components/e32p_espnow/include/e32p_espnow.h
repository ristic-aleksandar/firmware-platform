#pragma once
/**
 * @file e32p_espnow.h
 * @brief Thin facade over esp_now: connectionless peer-to-peer messaging.
 *        Brings up the Wi-Fi radio (STA, unconnected) internally. No router,
 *        no IP stack. The public API is free of esp_now types.
 */
#include <stdint.h>
#include <stddef.h>
#include "e32p_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Receive callback: sender MAC, payload, length. Runs on the Wi-Fi task
 *         — keep it short. */
typedef void (*e32p_espnow_recv_cb_t)(const uint8_t src_mac[6], const uint8_t *data, int len);

/**
 * @brief Initialize ESP-NOW (starts the Wi-Fi radio in STA mode, no AP needed).
 * @param[in] on_recv  Callback for incoming messages (may be NULL).
 * @return ESP_OK or a driver error.
 */
esp_err_t e32p_espnow_init(e32p_espnow_recv_cb_t on_recv);
esp_err_t e32p_espnow_deinit(void);

/** @brief Register a unicast peer (uses the current channel). */
esp_err_t e32p_espnow_add_peer(const uint8_t mac[6]);
esp_err_t e32p_espnow_remove_peer(const uint8_t mac[6]);

/** @brief Send to a specific peer (add it first). Max 250 bytes. */
esp_err_t e32p_espnow_send(const uint8_t mac[6], const void *data, size_t len);
/** @brief Broadcast to all listeners (auto-registers the broadcast peer). Max 250 bytes. */
esp_err_t e32p_espnow_broadcast(const void *data, size_t len);

/** @brief This device's STA MAC (the address peers send to). */
esp_err_t e32p_espnow_get_mac(uint8_t out_mac[6]);

#ifdef __cplusplus
}
#endif
