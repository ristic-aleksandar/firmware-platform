/**
 * @file e32p_spi.c
 * @brief e32p_spi implementation — bus + device facade over driver/spi_master.h.
 */
#include <stdlib.h>
#include "esp_log.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "e32p_spi.h"

static const char *TAG = "e32p_spi";

struct e32p_spi_bus_ctx {
    spi_host_device_t host;
};

struct e32p_spi_dev_ctx {
    spi_device_handle_t handle;
};

static spi_host_device_t map_host(int h)
{
    return (h == 3) ? SPI3_HOST : SPI2_HOST;
}

esp_err_t e32p_spi_bus_init(const e32p_spi_bus_config_t *cfg, e32p_spi_bus_handle_t *out)
{
    E32P_CHECK_ARG(cfg != NULL, TAG);
    E32P_CHECK_ARG(out != NULL, TAG);
    E32P_CHECK_ARG(cfg->host == 2 || cfg->host == 3, TAG);
    E32P_CHECK_ARG(GPIO_IS_VALID_OUTPUT_GPIO(cfg->sclk_pin), TAG);
    E32P_CHECK_ARG(GPIO_IS_VALID_OUTPUT_GPIO(cfg->mosi_pin), TAG);
    E32P_CHECK_ARG(cfg->miso_pin < 0 || GPIO_IS_VALID_GPIO(cfg->miso_pin), TAG);

    const spi_bus_config_t bc = {
        .sclk_io_num     = cfg->sclk_pin,
        .mosi_io_num     = cfg->mosi_pin,
        .miso_io_num     = cfg->miso_pin,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = cfg->max_transfer_sz,
    };
    const spi_host_device_t host = map_host(cfg->host);
    E32P_RETURN_ON_ERR(spi_bus_initialize(host, &bc, SPI_DMA_CH_AUTO), TAG);

    struct e32p_spi_bus_ctx *ctx = calloc(1, sizeof(*ctx));
    if (ctx == NULL) {
        spi_bus_free(host);
        return ESP_ERR_NO_MEM;
    }
    ctx->host = host;
    *out = ctx;
    ESP_LOGI(TAG, "spi%d bus init sclk=%d mosi=%d miso=%d", cfg->host,
             cfg->sclk_pin, cfg->mosi_pin, cfg->miso_pin);
    return ESP_OK;
}

esp_err_t e32p_spi_bus_deinit(e32p_spi_bus_handle_t bus)
{
    if (bus == NULL) {
        return ESP_OK;
    }
    spi_bus_free(bus->host);
    free(bus);
    return ESP_OK;
}

esp_err_t e32p_spi_add_device(e32p_spi_bus_handle_t bus, const e32p_spi_dev_config_t *dcfg,
                              e32p_spi_dev_handle_t *out)
{
    E32P_CHECK_ARG(bus != NULL, TAG);
    E32P_CHECK_ARG(dcfg != NULL, TAG);
    E32P_CHECK_ARG(out != NULL, TAG);
    E32P_CHECK_ARG(dcfg->mode <= 3, TAG);
    E32P_CHECK_ARG(dcfg->clock_hz > 0, TAG);
    E32P_CHECK_ARG(dcfg->queue_size >= 1, TAG);

    const spi_device_interface_config_t ic = {
        .clock_speed_hz = (int)dcfg->clock_hz,
        .mode           = dcfg->mode,
        .spics_io_num   = dcfg->cs_pin,
        .queue_size     = dcfg->queue_size,
    };
    spi_device_handle_t dev = NULL;
    E32P_RETURN_ON_ERR(spi_bus_add_device(bus->host, &ic, &dev), TAG);

    struct e32p_spi_dev_ctx *ctx = calloc(1, sizeof(*ctx));
    if (ctx == NULL) {
        spi_bus_remove_device(dev);
        return ESP_ERR_NO_MEM;
    }
    ctx->handle = dev;
    *out = ctx;
    return ESP_OK;
}

esp_err_t e32p_spi_remove_device(e32p_spi_dev_handle_t dev)
{
    if (dev == NULL) {
        return ESP_OK;
    }
    spi_bus_remove_device(dev->handle);
    free(dev);
    return ESP_OK;
}

esp_err_t e32p_spi_transfer(e32p_spi_dev_handle_t dev, const void *tx, void *rx, size_t len)
{
    E32P_CHECK_ARG(dev != NULL, TAG);
    E32P_CHECK_ARG(len > 0, TAG);
    spi_transaction_t t = {
        .length    = len * 8,   /* transaction length is in bits */
        .tx_buffer = tx,
        .rx_buffer = rx,
    };
    return spi_device_transmit(dev->handle, &t);
}

esp_err_t e32p_spi_write(e32p_spi_dev_handle_t dev, const void *tx, size_t len)
{
    E32P_CHECK_ARG(tx != NULL, TAG);
    return e32p_spi_transfer(dev, tx, NULL, len);
}

esp_err_t e32p_spi_read(e32p_spi_dev_handle_t dev, void *rx, size_t len)
{
    E32P_CHECK_ARG(rx != NULL, TAG);
    return e32p_spi_transfer(dev, NULL, rx, len);
}
