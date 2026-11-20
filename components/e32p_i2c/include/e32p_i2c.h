#pragma once
/**
 * @file e32p_i2c.h
 * @brief Thin facade over driver/i2c_master.h (ESP-IDF v5.x). A bus handle owns
 *        the port; device handles are attached to a bus for transfers.
 */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "e32p_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Opaque master-bus handle. */
typedef struct e32p_i2c_bus_ctx *e32p_i2c_bus_handle_t;
/** @brief Opaque device handle (attached to a bus). */
typedef struct e32p_i2c_dev_ctx *e32p_i2c_dev_handle_t;

/** @brief Master-bus configuration. */
typedef struct {
    int  port;             /*!< 0 or 1 (ESP32 has two I2C ports). */
    int  sda_pin;          /*!< SDA GPIO (output-capable). */
    int  scl_pin;          /*!< SCL GPIO (output-capable). */
    bool enable_pullups;   /*!< Weak internal pull-ups (external ones preferred). */
} e32p_i2c_bus_config_t;

/**
 * @brief Create a master bus.
 * @param[in]  cfg  Bus configuration (must not be NULL).
 * @param[out] out  Receives the bus handle.
 * @return ESP_OK, ESP_ERR_INVALID_ARG, ESP_ERR_NO_MEM, or a driver error.
 */
esp_err_t e32p_i2c_bus_init(const e32p_i2c_bus_config_t *cfg, e32p_i2c_bus_handle_t *out);

/** @brief Delete a master bus (NULL is a no-op). Remove its devices first. */
esp_err_t e32p_i2c_bus_deinit(e32p_i2c_bus_handle_t bus);

/**
 * @brief Probe one 7-bit address.
 * @return ESP_OK if a device ACKs, ESP_ERR_NOT_FOUND if none, or a driver error.
 */
esp_err_t e32p_i2c_probe(e32p_i2c_bus_handle_t bus, uint8_t addr, uint32_t timeout_ms);

/**
 * @brief Scan 0x08..0x77 and collect addresses that ACK.
 * @param[in]  bus        Valid bus.
 * @param[out] found      Array to fill (may be NULL to only count).
 * @param[in]  max        Capacity of @p found.
 * @param[out] out_count  Number of devices found (may exceed @p max).
 * @return ESP_OK or ESP_ERR_INVALID_ARG.
 */
esp_err_t e32p_i2c_scan(e32p_i2c_bus_handle_t bus, uint8_t *found, size_t max, size_t *out_count);

/**
 * @brief Attach a device to the bus.
 * @param[in]  bus           Valid bus.
 * @param[in]  addr          7-bit device address.
 * @param[in]  scl_speed_hz  Clock for this device (e.g. 100000 or 400000).
 * @param[out] out           Receives the device handle.
 * @return ESP_OK, ESP_ERR_INVALID_ARG, ESP_ERR_NO_MEM, or a driver error.
 */
esp_err_t e32p_i2c_add_device(e32p_i2c_bus_handle_t bus, uint8_t addr,
                              uint32_t scl_speed_hz, e32p_i2c_dev_handle_t *out);

/** @brief Detach a device (NULL is a no-op). */
esp_err_t e32p_i2c_remove_device(e32p_i2c_dev_handle_t dev);

/** @brief Write bytes to a device. */
esp_err_t e32p_i2c_write(e32p_i2c_dev_handle_t dev, const uint8_t *data, size_t len, uint32_t timeout_ms);
/** @brief Read bytes from a device. */
esp_err_t e32p_i2c_read(e32p_i2c_dev_handle_t dev, uint8_t *buf, size_t len, uint32_t timeout_ms);
/** @brief Write then read (repeated start) in one transaction. */
esp_err_t e32p_i2c_write_read(e32p_i2c_dev_handle_t dev, const uint8_t *wbuf, size_t wlen,
                              uint8_t *rbuf, size_t rlen, uint32_t timeout_ms);

/** @brief Read @p len bytes starting at register @p reg (write reg, repeated-start read). */
esp_err_t e32p_i2c_read_reg(e32p_i2c_dev_handle_t dev, uint8_t reg, uint8_t *buf, size_t len, uint32_t timeout_ms);
/** @brief Write one byte to register @p reg. */
esp_err_t e32p_i2c_write_reg(e32p_i2c_dev_handle_t dev, uint8_t reg, uint8_t value, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
