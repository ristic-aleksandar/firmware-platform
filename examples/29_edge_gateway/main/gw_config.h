#pragma once
/**
 * @file gw_config.h
 * @brief Gateway configuration persisted in NVS: what network to join, which
 *        broker to talk to, this gateway's id, and the telemetry period.
 *
 * The config is written either by BLE provisioning (gw_provision) or by an
 * `interval` command at runtime, and read back on every boot. An unprovisioned
 * device (no SSID/broker) drops into BLE provisioning mode.
 */
#include <stdbool.h>
#include "e32p_core.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GW_SSID_MAX    33   /*!< 32-char SSID + NUL. */
#define GW_PASS_MAX    65   /*!< 64-char passphrase + NUL. */
#define GW_BROKER_MAX  128  /*!< e.g. "mqtt://192.168.1.10:1883". */
#define GW_ID_MAX      24   /*!< Gateway id, e.g. "gw-01". */

/** @brief The full gateway configuration. */
typedef struct {
    char    ssid[GW_SSID_MAX];
    char    pass[GW_PASS_MAX];
    char    broker[GW_BROKER_MAX];
    char    id[GW_ID_MAX];
    int32_t interval_s;   /*!< Telemetry publish period, seconds (>= 1). */
} gw_config_t;

/**
 * @brief Load the config from NVS, filling defaults for any missing key
 *        (id="gw-01", interval=5, everything else empty). Never fails hard —
 *        a fresh device just comes back unprovisioned.
 * @return ESP_OK.
 */
esp_err_t gw_config_load(gw_config_t *out);

/** @brief Persist the whole config to NVS (commit included). */
esp_err_t gw_config_save(const gw_config_t *cfg);

/** @brief True once the minimum to run is present (SSID + broker). */
bool gw_config_is_provisioned(const gw_config_t *cfg);

#ifdef __cplusplus
}
#endif
