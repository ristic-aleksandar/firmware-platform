/**
 * @file e32p_i2c.c
 * @brief e32p_i2c implementation — bus + device facade over driver/i2c_master.h.
 */
#include <stdlib.h>
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "e32p_i2c.h"

static const char *TAG = "e32p_i2c";

struct e32p_i2c_bus_ctx {
    i2c_master_bus_handle_t handle;
};

struct e32p_i2c_dev_ctx {
    i2c_master_dev_handle_t handle;
};

esp_err_t e32p_i2c_bus_init(const e32p_i2c_bus_config_t *cfg, e32p_i2c_bus_handle_t *out)
{
    E32P_CHECK_ARG(cfg != NULL, TAG);
    E32P_CHECK_ARG(out != NULL, TAG);
    E32P_CHECK_ARG(GPIO_IS_VALID_OUTPUT_GPIO(cfg->sda_pin), TAG);
    E32P_CHECK_ARG(GPIO_IS_VALID_OUTPUT_GPIO(cfg->scl_pin), TAG);

    const i2c_master_bus_config_t bc = {
        .i2c_port          = cfg->port,
        .sda_io_num        = cfg->sda_pin,
        .scl_io_num        = cfg->scl_pin,
        .clk_source        = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = cfg->enable_pullups,
    };
    i2c_master_bus_handle_t bus = NULL;
    E32P_RETURN_ON_ERR(i2c_new_master_bus(&bc, &bus), TAG);

    struct e32p_i2c_bus_ctx *ctx = calloc(1, sizeof(*ctx));
    if (ctx == NULL) {
        i2c_del_master_bus(bus);
        return ESP_ERR_NO_MEM;
    }
    ctx->handle = bus;
    *out = ctx;
    ESP_LOGI(TAG, "i2c%d bus init sda=%d scl=%d", cfg->port, cfg->sda_pin, cfg->scl_pin);
    return ESP_OK;
}

esp_err_t e32p_i2c_bus_deinit(e32p_i2c_bus_handle_t bus)
{
    if (bus == NULL) {
        return ESP_OK;
    }
    i2c_del_master_bus(bus->handle);
    free(bus);
    return ESP_OK;
}

esp_err_t e32p_i2c_probe(e32p_i2c_bus_handle_t bus, uint8_t addr, uint32_t timeout_ms)
{
    E32P_CHECK_ARG(bus != NULL, TAG);
    return i2c_master_probe(bus->handle, addr, (int)timeout_ms);
}

esp_err_t e32p_i2c_scan(e32p_i2c_bus_handle_t bus, uint8_t *found, size_t max, size_t *out_count)
{
    E32P_CHECK_ARG(bus != NULL, TAG);
    E32P_CHECK_ARG(out_count != NULL, TAG);

    size_t n = 0;
    for (uint8_t a = 0x08; a <= 0x77; a++) {
        if (i2c_master_probe(bus->handle, a, 50) == ESP_OK) {
            if (found != NULL && n < max) {
                found[n] = a;
            }
            n++;
        }
    }
    *out_count = n;
    return ESP_OK;
}

esp_err_t e32p_i2c_add_device(e32p_i2c_bus_handle_t bus, uint8_t addr,
                              uint32_t scl_speed_hz, e32p_i2c_dev_handle_t *out)
{
    E32P_CHECK_ARG(bus != NULL, TAG);
    E32P_CHECK_ARG(out != NULL, TAG);
    E32P_CHECK_ARG(scl_speed_hz > 0, TAG);

    const i2c_device_config_t dc = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = addr,
        .scl_speed_hz    = scl_speed_hz,
    };
    i2c_master_dev_handle_t dev = NULL;
    E32P_RETURN_ON_ERR(i2c_master_bus_add_device(bus->handle, &dc, &dev), TAG);

    struct e32p_i2c_dev_ctx *ctx = calloc(1, sizeof(*ctx));
    if (ctx == NULL) {
        i2c_master_bus_rm_device(dev);
        return ESP_ERR_NO_MEM;
    }
    ctx->handle = dev;
    *out = ctx;
    return ESP_OK;
}

esp_err_t e32p_i2c_remove_device(e32p_i2c_dev_handle_t dev)
{
    if (dev == NULL) {
        return ESP_OK;
    }
    i2c_master_bus_rm_device(dev->handle);
    free(dev);
    return ESP_OK;
}

esp_err_t e32p_i2c_write(e32p_i2c_dev_handle_t dev, const uint8_t *data, size_t len, uint32_t timeout_ms)
{
    E32P_CHECK_ARG(dev != NULL, TAG);
    E32P_CHECK_ARG(data != NULL, TAG);
    return i2c_master_transmit(dev->handle, data, len, (int)timeout_ms);
}

esp_err_t e32p_i2c_read(e32p_i2c_dev_handle_t dev, uint8_t *buf, size_t len, uint32_t timeout_ms)
{
    E32P_CHECK_ARG(dev != NULL, TAG);
    E32P_CHECK_ARG(buf != NULL, TAG);
    return i2c_master_receive(dev->handle, buf, len, (int)timeout_ms);
}

esp_err_t e32p_i2c_write_read(e32p_i2c_dev_handle_t dev, const uint8_t *wbuf, size_t wlen,
                              uint8_t *rbuf, size_t rlen, uint32_t timeout_ms)
{
    E32P_CHECK_ARG(dev != NULL, TAG);
    E32P_CHECK_ARG(wbuf != NULL, TAG);
    E32P_CHECK_ARG(rbuf != NULL, TAG);
    return i2c_master_transmit_receive(dev->handle, wbuf, wlen, rbuf, rlen, (int)timeout_ms);
}

esp_err_t e32p_i2c_read_reg(e32p_i2c_dev_handle_t dev, uint8_t reg, uint8_t *buf, size_t len, uint32_t timeout_ms)
{
    E32P_CHECK_ARG(dev != NULL, TAG);
    E32P_CHECK_ARG(buf != NULL, TAG);
    return i2c_master_transmit_receive(dev->handle, &reg, 1, buf, len, (int)timeout_ms);
}

esp_err_t e32p_i2c_write_reg(e32p_i2c_dev_handle_t dev, uint8_t reg, uint8_t value, uint32_t timeout_ms)
{
    E32P_CHECK_ARG(dev != NULL, TAG);
    const uint8_t payload[2] = { reg, value };
    return i2c_master_transmit(dev->handle, payload, sizeof(payload), (int)timeout_ms);
}
