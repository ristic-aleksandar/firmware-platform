#pragma once
/**
 * @file e32p_spi.h
 * @brief Thin facade over driver/spi_master.h (v5.x). A bus handle owns the
 *        host; device handles (own CS/clock/mode) attach to it for transfers.
 */
#include <stdint.h>
#include <stddef.h>
#include "e32p_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Opaque SPI-bus (host) handle. */
typedef struct e32p_spi_bus_ctx *e32p_spi_bus_handle_t;
/** @brief Opaque SPI-device handle. */
typedef struct e32p_spi_dev_ctx *e32p_spi_dev_handle_t;

/** @brief Master-bus (host) configuration. */
typedef struct {
    int host;             /*!< 2 (SPI2/HSPI) or 3 (SPI3/VSPI). SPI1 is flash. */
    int sclk_pin;         /*!< Output-capable GPIO. */
    int mosi_pin;         /*!< Output-capable GPIO. */
    int miso_pin;         /*!< Valid GPIO, or -1 for write-only buses. */
    int max_transfer_sz;  /*!< 0 = driver default (4092 bytes). */
} e32p_spi_bus_config_t;

/** @brief Per-device configuration. */
typedef struct {
    int      cs_pin;      /*!< Chip-select GPIO, or -1 if driven externally. */
    uint32_t clock_hz;    /*!< SCLK frequency, e.g. 1000000. */
    uint8_t  mode;        /*!< SPI mode 0..3 (CPOL/CPHA). */
    int      queue_size;  /*!< Pending transaction slots (>= 1). */
} e32p_spi_dev_config_t;

/**
 * @brief Initialize a SPI host bus.
 * @param[in]  cfg  Bus configuration (must not be NULL).
 * @param[out] out  Receives the bus handle.
 * @return ESP_OK, ESP_ERR_INVALID_ARG, ESP_ERR_NO_MEM, or a driver error.
 */
esp_err_t e32p_spi_bus_init(const e32p_spi_bus_config_t *cfg, e32p_spi_bus_handle_t *out);

/** @brief Free the bus (NULL is a no-op). Remove its devices first. */
esp_err_t e32p_spi_bus_deinit(e32p_spi_bus_handle_t bus);

/**
 * @brief Attach a device to the bus.
 * @param[in]  bus   Valid bus.
 * @param[in]  dcfg  Device configuration (must not be NULL).
 * @param[out] out   Receives the device handle.
 * @return ESP_OK, ESP_ERR_INVALID_ARG, ESP_ERR_NO_MEM, or a driver error.
 */
esp_err_t e32p_spi_add_device(e32p_spi_bus_handle_t bus, const e32p_spi_dev_config_t *dcfg,
                              e32p_spi_dev_handle_t *out);

/** @brief Detach a device (NULL is a no-op). */
esp_err_t e32p_spi_remove_device(e32p_spi_dev_handle_t dev);

/**
 * @brief Full-duplex transfer: clock @p len bytes, sending from @p tx while
 *        receiving into @p rx. Either buffer may be NULL for one-way.
 * @return ESP_OK, ESP_ERR_INVALID_ARG, or a driver error.
 */
esp_err_t e32p_spi_transfer(e32p_spi_dev_handle_t dev, const void *tx, void *rx, size_t len);

/** @brief Write @p len bytes (received data discarded). */
esp_err_t e32p_spi_write(e32p_spi_dev_handle_t dev, const void *tx, size_t len);

/** @brief Read @p len bytes (MOSI driven as zeros). */
esp_err_t e32p_spi_read(e32p_spi_dev_handle_t dev, void *rx, size_t len);

#ifdef __cplusplus
}
#endif
