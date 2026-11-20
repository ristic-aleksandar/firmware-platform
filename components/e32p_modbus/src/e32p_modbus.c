/**
 * @file e32p_modbus.c
 * @brief e32p_modbus implementation — pure protocol + slave core + UART master.
 */
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "e32p_modbus.h"

static const char *TAG = "e32p_modbus";

struct e32p_modbus_master_ctx {
    e32p_uart_handle_t uart;
    uint32_t           timeout_ms;
    uint8_t            last_exception;
};

/* ---- Layer 1: pure protocol ---------------------------------------------- */

uint16_t e32p_modbus_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 1) ? (uint16_t)((crc >> 1) ^ 0xA001) : (uint16_t)(crc >> 1);
        }
    }
    return crc;
}

int e32p_modbus_adu_build(uint8_t *dst, size_t cap, uint8_t slave, uint8_t func,
                          const uint8_t *payload, size_t plen)
{
    if (dst == NULL || (plen > 0 && payload == NULL)) {
        return -1;
    }
    size_t total = 2 + plen + 2;   /* slave + func + payload + crc16 */
    if (total > cap) {
        return -1;
    }
    dst[0] = slave;
    dst[1] = func;
    if (plen > 0) {
        memcpy(&dst[2], payload, plen);
    }
    uint16_t crc = e32p_modbus_crc16(dst, 2 + plen);
    dst[2 + plen] = (uint8_t)(crc & 0xFF);        /* CRC low byte first (RTU) */
    dst[3 + plen] = (uint8_t)(crc >> 8);
    return (int)total;
}

bool e32p_modbus_adu_check(const uint8_t *adu, size_t len)
{
    if (adu == NULL || len < 4) {
        return false;
    }
    uint16_t crc = e32p_modbus_crc16(adu, len - 2);
    return adu[len - 2] == (uint8_t)(crc & 0xFF) && adu[len - 1] == (uint8_t)(crc >> 8);
}

int e32p_modbus_build_read(uint8_t *dst, size_t cap, uint8_t slave, uint8_t func,
                           uint16_t addr, uint16_t count)
{
    const uint8_t payload[4] = {
        (uint8_t)(addr >> 8), (uint8_t)(addr & 0xFF),
        (uint8_t)(count >> 8), (uint8_t)(count & 0xFF),
    };
    return e32p_modbus_adu_build(dst, cap, slave, func, payload, sizeof(payload));
}

int e32p_modbus_build_write_single(uint8_t *dst, size_t cap, uint8_t slave,
                                   uint16_t addr, uint16_t value)
{
    const uint8_t payload[4] = {
        (uint8_t)(addr >> 8), (uint8_t)(addr & 0xFF),
        (uint8_t)(value >> 8), (uint8_t)(value & 0xFF),
    };
    return e32p_modbus_adu_build(dst, cap, slave, E32P_MODBUS_FC_WRITE_SINGLE, payload, sizeof(payload));
}

int e32p_modbus_build_write_multiple(uint8_t *dst, size_t cap, uint8_t slave,
                                     uint16_t addr, uint16_t count, const uint16_t *values)
{
    if (values == NULL || count < 1 || count > E32P_MODBUS_MAX_WRITE_REGS) {
        return -1;
    }
    uint8_t payload[5 + 2 * E32P_MODBUS_MAX_WRITE_REGS];
    payload[0] = (uint8_t)(addr >> 8);
    payload[1] = (uint8_t)(addr & 0xFF);
    payload[2] = (uint8_t)(count >> 8);
    payload[3] = (uint8_t)(count & 0xFF);
    payload[4] = (uint8_t)(count * 2);   /* byte count */
    for (uint16_t i = 0; i < count; i++) {
        payload[5 + 2 * i] = (uint8_t)(values[i] >> 8);
        payload[6 + 2 * i] = (uint8_t)(values[i] & 0xFF);
    }
    return e32p_modbus_adu_build(dst, cap, slave, E32P_MODBUS_FC_WRITE_MULTIPLE, payload, 5 + 2 * count);
}

esp_err_t e32p_modbus_parse_read_response(const uint8_t *adu, size_t len, uint8_t slave, uint8_t func,
                                          uint16_t *out, size_t max, size_t *out_count, uint8_t *out_exc)
{
    if (out_exc) {
        *out_exc = 0;
    }
    if (!e32p_modbus_adu_check(adu, len)) {
        return ESP_ERR_INVALID_CRC;
    }
    if (adu[0] != slave) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (adu[1] == (func | E32P_MODBUS_FC_EXCEPTION_BIT)) {
        if (out_exc) {
            *out_exc = adu[2];
        }
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (adu[1] != func) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    uint8_t byte_count = adu[2];
    size_t nregs = byte_count / 2u;
    if ((byte_count & 1) || (size_t)(3 + byte_count + 2) != len) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (nregs > max) {
        return ESP_ERR_INVALID_SIZE;
    }
    for (size_t i = 0; i < nregs; i++) {
        out[i] = (uint16_t)((adu[3 + 2 * i] << 8) | adu[4 + 2 * i]);
    }
    if (out_count) {
        *out_count = nregs;
    }
    return ESP_OK;
}

/* ---- Layer 2: pure slave request handler --------------------------------- */

static int slave_exception(uint8_t *resp, size_t cap, uint8_t addr, uint8_t func, uint8_t exc)
{
    return e32p_modbus_adu_build(resp, cap, addr, (uint8_t)(func | E32P_MODBUS_FC_EXCEPTION_BIT), &exc, 1);
}

int e32p_modbus_slave_process(e32p_modbus_slave_t *slave, const uint8_t *req, size_t req_len,
                              uint8_t *resp, size_t cap)
{
    if (slave == NULL || req == NULL || resp == NULL || req_len < 4 || !e32p_modbus_adu_check(req, req_len)) {
        return -1;
    }
    uint8_t addr = req[0];
    uint8_t func = req[1];
    if (addr != slave->address && addr != E32P_MODBUS_ADDR_BROADCAST) {
        return 0;   /* not addressed to us */
    }
    bool broadcast = (addr == E32P_MODBUS_ADDR_BROADCAST);

    switch (func) {
    case E32P_MODBUS_FC_READ_HOLDING:
    case E32P_MODBUS_FC_READ_INPUT: {
        if (req_len != 8) {
            return slave_exception(resp, cap, addr, func, E32P_MODBUS_EXC_ILLEGAL_DATA_VALUE);
        }
        uint16_t start = (uint16_t)((req[2] << 8) | req[3]);
        uint16_t count = (uint16_t)((req[4] << 8) | req[5]);
        const uint16_t *bank = (func == E32P_MODBUS_FC_READ_HOLDING) ? slave->holding : slave->input;
        size_t nbank = (func == E32P_MODBUS_FC_READ_HOLDING) ? slave->n_holding : slave->n_input;
        if (count < 1 || count > E32P_MODBUS_MAX_READ_REGS) {
            return slave_exception(resp, cap, addr, func, E32P_MODBUS_EXC_ILLEGAL_DATA_VALUE);
        }
        if (bank == NULL || (size_t)start + count > nbank) {
            return slave_exception(resp, cap, addr, func, E32P_MODBUS_EXC_ILLEGAL_DATA_ADDRESS);
        }
        uint8_t payload[1 + 2 * E32P_MODBUS_MAX_READ_REGS];
        payload[0] = (uint8_t)(count * 2);
        for (uint16_t i = 0; i < count; i++) {
            payload[1 + 2 * i] = (uint8_t)(bank[start + i] >> 8);
            payload[2 + 2 * i] = (uint8_t)(bank[start + i] & 0xFF);
        }
        return e32p_modbus_adu_build(resp, cap, addr, func, payload, 1 + 2 * count);
    }

    case E32P_MODBUS_FC_WRITE_SINGLE: {
        if (req_len != 8) {
            return slave_exception(resp, cap, addr, func, E32P_MODBUS_EXC_ILLEGAL_DATA_VALUE);
        }
        uint16_t a = (uint16_t)((req[2] << 8) | req[3]);
        uint16_t v = (uint16_t)((req[4] << 8) | req[5]);
        if (slave->holding == NULL || a >= slave->n_holding) {
            return slave_exception(resp, cap, addr, func, E32P_MODBUS_EXC_ILLEGAL_DATA_ADDRESS);
        }
        slave->holding[a] = v;
        if (broadcast) {
            return 0;
        }
        const uint8_t echo[4] = { req[2], req[3], req[4], req[5] };
        return e32p_modbus_adu_build(resp, cap, addr, func, echo, sizeof(echo));
    }

    case E32P_MODBUS_FC_WRITE_MULTIPLE: {
        if (req_len < 9) {
            return slave_exception(resp, cap, addr, func, E32P_MODBUS_EXC_ILLEGAL_DATA_VALUE);
        }
        uint16_t a = (uint16_t)((req[2] << 8) | req[3]);
        uint16_t count = (uint16_t)((req[4] << 8) | req[5]);
        uint8_t byte_count = req[6];
        if (count < 1 || count > E32P_MODBUS_MAX_WRITE_REGS || byte_count != count * 2 ||
            req_len != (size_t)(9 + byte_count)) {
            return slave_exception(resp, cap, addr, func, E32P_MODBUS_EXC_ILLEGAL_DATA_VALUE);
        }
        if (slave->holding == NULL || (size_t)a + count > slave->n_holding) {
            return slave_exception(resp, cap, addr, func, E32P_MODBUS_EXC_ILLEGAL_DATA_ADDRESS);
        }
        for (uint16_t i = 0; i < count; i++) {
            slave->holding[a + i] = (uint16_t)((req[7 + 2 * i] << 8) | req[8 + 2 * i]);
        }
        if (broadcast) {
            return 0;
        }
        const uint8_t echo[4] = { req[2], req[3], req[4], req[5] };
        return e32p_modbus_adu_build(resp, cap, addr, func, echo, sizeof(echo));
    }

    default:
        return slave_exception(resp, cap, addr, func, E32P_MODBUS_EXC_ILLEGAL_FUNCTION);
    }
}

/* ---- Layer 3: master over UART ------------------------------------------- */

esp_err_t e32p_modbus_master_init(const e32p_modbus_master_config_t *cfg, e32p_modbus_master_handle_t *out)
{
    E32P_CHECK_ARG(cfg != NULL, TAG);
    E32P_CHECK_ARG(out != NULL, TAG);

    e32p_uart_config_t uc = E32P_UART_CONFIG_DEFAULT();
    uc.port = cfg->uart_port;
    uc.tx_pin = cfg->tx_pin;
    uc.rx_pin = cfg->rx_pin;
    uc.baud_rate = cfg->baud;
    uc.data_bits = 8;
    uc.parity = cfg->parity;
    uc.stop_bits = cfg->stop_bits;

    e32p_uart_handle_t uart = NULL;
    E32P_RETURN_ON_ERR(e32p_uart_init(&uc, &uart), TAG);

    struct e32p_modbus_master_ctx *ctx = calloc(1, sizeof(*ctx));
    if (ctx == NULL) {
        e32p_uart_deinit(uart);
        return ESP_ERR_NO_MEM;
    }
    ctx->uart = uart;
    ctx->timeout_ms = cfg->response_timeout_ms ? cfg->response_timeout_ms : 500;
    ESP_LOGI(TAG, "modbus master on uart%d tx=%d rx=%d %lu baud",
             cfg->uart_port, cfg->tx_pin, cfg->rx_pin, (unsigned long)cfg->baud);
    *out = ctx;
    return ESP_OK;
}

esp_err_t e32p_modbus_master_deinit(e32p_modbus_master_handle_t h)
{
    if (h == NULL) {
        return ESP_OK;
    }
    e32p_uart_deinit(h->uart);
    free(h);
    return ESP_OK;
}

/* Send a request and read the reply. @p normal_len is the expected length of a
   non-exception reply; an exception reply is always 5 bytes. */
static esp_err_t transact(struct e32p_modbus_master_ctx *c, const uint8_t *req, size_t req_len,
                          uint8_t *resp, size_t normal_len, size_t cap, size_t *out_len)
{
    e32p_uart_flush_input(c->uart);
    if (e32p_uart_write(c->uart, req, req_len) != (int)req_len) {
        return ESP_FAIL;
    }
    if (e32p_uart_read(c->uart, resp, 2, c->timeout_ms) < 2) {
        return ESP_ERR_TIMEOUT;
    }
    size_t want = (resp[1] & E32P_MODBUS_FC_EXCEPTION_BIT) ? 5 : normal_len;
    if (want > cap) {
        return ESP_ERR_INVALID_SIZE;
    }
    size_t remaining = want - 2;
    if (remaining > 0) {
        if (e32p_uart_read(c->uart, resp + 2, remaining, c->timeout_ms) < (int)remaining) {
            return ESP_ERR_TIMEOUT;
        }
    }
    *out_len = want;
    return ESP_OK;
}

static esp_err_t read_registers(e32p_modbus_master_handle_t h, uint8_t func, uint8_t slave,
                                uint16_t addr, uint16_t count, uint16_t *out)
{
    E32P_CHECK_ARG(h != NULL, TAG);
    E32P_CHECK_ARG(out != NULL, TAG);
    E32P_CHECK_ARG(count >= 1 && count <= E32P_MODBUS_MAX_READ_REGS, TAG);
    E32P_CHECK_ARG(slave <= E32P_MODBUS_ADDR_MAX, TAG);
    h->last_exception = 0;

    uint8_t req[8];
    int rl = e32p_modbus_build_read(req, sizeof(req), slave, func, addr, count);
    uint8_t resp[5 + 2 * E32P_MODBUS_MAX_READ_REGS];
    size_t rlen = 0;
    E32P_RETURN_ON_ERR(transact(h, req, rl, resp, (size_t)(5 + 2 * count), sizeof(resp), &rlen), TAG);

    uint8_t exc = 0;
    esp_err_t err = e32p_modbus_parse_read_response(resp, rlen, slave, func, out, count, NULL, &exc);
    h->last_exception = exc;
    return err;
}

esp_err_t e32p_modbus_read_holding_registers(e32p_modbus_master_handle_t h, uint8_t slave,
                                             uint16_t addr, uint16_t count, uint16_t *out)
{
    return read_registers(h, E32P_MODBUS_FC_READ_HOLDING, slave, addr, count, out);
}

esp_err_t e32p_modbus_read_input_registers(e32p_modbus_master_handle_t h, uint8_t slave,
                                           uint16_t addr, uint16_t count, uint16_t *out)
{
    return read_registers(h, E32P_MODBUS_FC_READ_INPUT, slave, addr, count, out);
}

/* Verify an echo-style write reply (0x06 / 0x10): CRC, slave, function/exception. */
static esp_err_t parse_write_response(const uint8_t *adu, size_t len, uint8_t slave, uint8_t func, uint8_t *exc)
{
    *exc = 0;
    if (!e32p_modbus_adu_check(adu, len)) {
        return ESP_ERR_INVALID_CRC;
    }
    if (adu[0] != slave) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (adu[1] == (func | E32P_MODBUS_FC_EXCEPTION_BIT)) {
        *exc = adu[2];
        return ESP_ERR_INVALID_RESPONSE;
    }
    return (adu[1] == func) ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

esp_err_t e32p_modbus_write_single_register(e32p_modbus_master_handle_t h, uint8_t slave,
                                            uint16_t addr, uint16_t value)
{
    E32P_CHECK_ARG(h != NULL, TAG);
    E32P_CHECK_ARG(slave <= E32P_MODBUS_ADDR_MAX, TAG);
    h->last_exception = 0;

    uint8_t req[8];
    int rl = e32p_modbus_build_write_single(req, sizeof(req), slave, addr, value);
    uint8_t resp[8];
    size_t rlen = 0;
    E32P_RETURN_ON_ERR(transact(h, req, rl, resp, 8, sizeof(resp), &rlen), TAG);

    uint8_t exc = 0;
    esp_err_t err = parse_write_response(resp, rlen, slave, E32P_MODBUS_FC_WRITE_SINGLE, &exc);
    h->last_exception = exc;
    return err;
}

esp_err_t e32p_modbus_write_multiple_registers(e32p_modbus_master_handle_t h, uint8_t slave,
                                               uint16_t addr, uint16_t count, const uint16_t *values)
{
    E32P_CHECK_ARG(h != NULL, TAG);
    E32P_CHECK_ARG(values != NULL, TAG);
    E32P_CHECK_ARG(count >= 1 && count <= E32P_MODBUS_MAX_WRITE_REGS, TAG);
    E32P_CHECK_ARG(slave <= E32P_MODBUS_ADDR_MAX, TAG);
    h->last_exception = 0;

    uint8_t req[9 + 2 * E32P_MODBUS_MAX_WRITE_REGS];
    int rl = e32p_modbus_build_write_multiple(req, sizeof(req), slave, addr, count, values);
    if (rl < 0) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t resp[8];
    size_t rlen = 0;
    E32P_RETURN_ON_ERR(transact(h, req, (size_t)rl, resp, 8, sizeof(resp), &rlen), TAG);

    uint8_t exc = 0;
    esp_err_t err = parse_write_response(resp, rlen, slave, E32P_MODBUS_FC_WRITE_MULTIPLE, &exc);
    h->last_exception = exc;
    return err;
}

uint8_t e32p_modbus_last_exception(e32p_modbus_master_handle_t h)
{
    return (h != NULL) ? h->last_exception : 0;
}
