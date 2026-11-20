#pragma once
/**
 * @file e32p_ble.h
 * @brief BLE GATT peripheral facade over Apache NimBLE: advertise a named device
 *        with one custom service and a few characteristics (read / write / notify).
 *
 * Split so the awkward-to-verify logic is testable without a radio:
 *   - PURE: advertising-payload (AD structure) assembly + UUID helpers. Always
 *     compiled; unit-tested headless.
 *   - RADIO: init / advertise / notify over NimBLE, compiled only when
 *     CONFIG_BT_NIMBLE_ENABLED is set (the demo enables it). Without it, the
 *     device functions return ESP_ERR_NOT_SUPPORTED.
 *
 * Flat/singleton (one BLE controller per chip); the public header is free of
 * NimBLE types.
 */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "e32p_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief A BLE UUID: 16-bit or 128-bit, stored little-endian (adv/att order). */
typedef struct {
    uint8_t bytes[16];
    uint8_t len;        /*!< 2 or 16. */
} e32p_ble_uuid_t;

/** @brief Characteristic property flags. */
#define E32P_BLE_PROP_READ    0x01
#define E32P_BLE_PROP_WRITE   0x02
#define E32P_BLE_PROP_NOTIFY  0x04

/** @brief Advertising flags octet (LE General Discoverable, BR/EDR unsupported). */
#define E32P_BLE_ADV_FLAGS_DEFAULT  0x06

#define E32P_BLE_MAX_CHARS  8    /*!< Characteristics per service. */
#define E32P_BLE_VAL_MAX    64   /*!< Max stored value per characteristic. */

/** @brief One characteristic in the service. */
typedef struct {
    e32p_ble_uuid_t uuid;
    uint8_t         props;   /*!< OR of E32P_BLE_PROP_*. */
} e32p_ble_char_t;

/** @brief Central wrote to characteristic @p idx (index into cfg->chars). */
typedef void (*e32p_ble_write_fn)(uint16_t idx, const uint8_t *data, uint16_t len, void *user);
/** @brief Connection state changed. */
typedef void (*e32p_ble_conn_fn)(bool connected, void *user);

/** @brief Peripheral configuration. */
typedef struct {
    const char           *device_name;   /*!< Advertised name. */
    e32p_ble_uuid_t       service_uuid;   /*!< The one primary service. */
    const e32p_ble_char_t *chars;         /*!< Characteristic table. */
    size_t                n_chars;        /*!< <= E32P_BLE_MAX_CHARS. */
    e32p_ble_write_fn     on_write;       /*!< Optional write callback. */
    e32p_ble_conn_fn      on_conn;        /*!< Optional connect/disconnect callback. */
    void                 *user;           /*!< Passed to the callbacks. */
} e32p_ble_config_t;

/* ---- Pure helpers (no radio; unit-testable) ------------------------------- */

/** @brief Build a 16-bit UUID. */
e32p_ble_uuid_t e32p_ble_uuid16(uint16_t uuid);

/** @brief Build a 128-bit UUID from 16 little-endian bytes. */
e32p_ble_uuid_t e32p_ble_uuid128(const uint8_t bytes[16]);

/**
 * @brief Assemble a legacy advertising payload (AD structures): Flags, the
 *        service UUID (if @p svc != NULL), then the Complete Local Name.
 * @param[out] dst    Output buffer.
 * @param[in]  cap    Capacity.
 * @param[in]  flags  Adv flags octet (e.g. E32P_BLE_ADV_FLAGS_DEFAULT).
 * @param[in]  name   Device name (may be NULL to omit).
 * @param[in]  svc    Service UUID to advertise (may be NULL to omit).
 * @return Payload length (<= 31), or -1 if it would exceed 31 bytes / @p cap.
 */
int e32p_ble_adv_build(uint8_t *dst, size_t cap, uint8_t flags,
                       const char *name, const e32p_ble_uuid_t *svc);

/* ---- Peripheral (NimBLE; needs CONFIG_BT_NIMBLE_ENABLED) ------------------- */

/**
 * @brief Initialize the controller + host, register the service, and start
 *        advertising (re-advertises after a disconnect).
 * @return ESP_OK; ESP_ERR_INVALID_ARG; ESP_ERR_NOT_SUPPORTED (BLE not built in);
 *         or a stack error.
 */
esp_err_t e32p_ble_init(const e32p_ble_config_t *cfg);

/** @brief Stop advertising and tear down the stack. */
esp_err_t e32p_ble_deinit(void);

/** @brief Start (or resume) advertising. */
esp_err_t e32p_ble_start_adv(void);

/** @brief Stop advertising. */
esp_err_t e32p_ble_stop_adv(void);

/**
 * @brief Set a characteristic's stored value (returned on a central read).
 * @return ESP_OK, ESP_ERR_INVALID_ARG, or ESP_ERR_NOT_SUPPORTED.
 */
esp_err_t e32p_ble_set_value(uint16_t idx, const void *data, uint16_t len);

/**
 * @brief Push a new value to a subscribed central (notification), and store it.
 * @return ESP_OK; ESP_ERR_INVALID_ARG; ESP_ERR_INVALID_STATE (no connection);
 *         or ESP_ERR_NOT_SUPPORTED.
 */
esp_err_t e32p_ble_notify(uint16_t idx, const void *data, uint16_t len);

/** @brief True while a central is connected. */
bool e32p_ble_is_connected(void);

#ifdef __cplusplus
}
#endif
