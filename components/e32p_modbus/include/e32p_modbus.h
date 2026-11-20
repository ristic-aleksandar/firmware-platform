#pragma once
/**
 * @file e32p_modbus.h
 * @brief Modbus RTU master over e32p_uart, plus a pure protocol core.
 *
 * Three layers:
 *   1. Pure protocol   — CRC-16/Modbus, ADU framing, request builders, response
 *                        parser. No UART; fully unit-testable.
 *   2. Pure slave core — process a request against a register bank and produce a
 *                        response ADU. Lets a full master<->slave exchange be
 *                        tested headless (and drives an in-firmware slave).
 *   3. Master (UART)   — read/write holding & input registers over a serial line.
 *
 * Supported function codes: 0x03 read holding, 0x04 read input, 0x06 write single
 * register, 0x10 write multiple registers.
 */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "e32p_core.h"
#include "e32p_uart.h"

#ifdef __cplusplus
extern "C" {
#endif

#define E32P_MODBUS_ADDR_BROADCAST  0     /*!< Unit id 0 = broadcast (writes only). */
#define E32P_MODBUS_ADDR_MAX        247   /*!< Valid unit ids are 1..247. */
#define E32P_MODBUS_ADU_MAX         256   /*!< Max RTU ADU size in bytes. */
#define E32P_MODBUS_MAX_READ_REGS   125   /*!< Max registers per read (0x03/0x04). */
#define E32P_MODBUS_MAX_WRITE_REGS  123   /*!< Max registers per write (0x10). */

#define E32P_MODBUS_FC_READ_HOLDING    0x03
#define E32P_MODBUS_FC_READ_INPUT      0x04
#define E32P_MODBUS_FC_WRITE_SINGLE    0x06
#define E32P_MODBUS_FC_WRITE_MULTIPLE  0x10
#define E32P_MODBUS_FC_EXCEPTION_BIT   0x80  /*!< ORed into the function on an error reply. */

/** @brief Standard Modbus exception codes (in an error reply's data byte). */
typedef enum {
    E32P_MODBUS_EXC_NONE                 = 0x00,
    E32P_MODBUS_EXC_ILLEGAL_FUNCTION     = 0x01,
    E32P_MODBUS_EXC_ILLEGAL_DATA_ADDRESS = 0x02,
    E32P_MODBUS_EXC_ILLEGAL_DATA_VALUE   = 0x03,
    E32P_MODBUS_EXC_SERVER_DEVICE_FAILURE = 0x04,
} e32p_modbus_exception_t;

/* ---- Layer 1: pure protocol ---------------------------------------------- */

/** @brief CRC-16/Modbus (poly 0xA001, init 0xFFFF) over @p len bytes. */
uint16_t e32p_modbus_crc16(const uint8_t *data, size_t len);

/**
 * @brief Build an RTU ADU: [slave][func][payload...][crc_lo][crc_hi].
 * @return Total ADU length, or -1 if @p cap is too small.
 */
int e32p_modbus_adu_build(uint8_t *dst, size_t cap, uint8_t slave, uint8_t func,
                          const uint8_t *payload, size_t plen);

/** @brief True if @p len >= 4 and the trailing CRC matches. */
bool e32p_modbus_adu_check(const uint8_t *adu, size_t len);

/** @brief Build a read request (0x03/0x04): start address + register count. */
int e32p_modbus_build_read(uint8_t *dst, size_t cap, uint8_t slave, uint8_t func,
                           uint16_t addr, uint16_t count);

/** @brief Build a write-single-register request (0x06). */
int e32p_modbus_build_write_single(uint8_t *dst, size_t cap, uint8_t slave,
                                   uint16_t addr, uint16_t value);

/** @brief Build a write-multiple-registers request (0x10). */
int e32p_modbus_build_write_multiple(uint8_t *dst, size_t cap, uint8_t slave,
                                     uint16_t addr, uint16_t count, const uint16_t *values);

/**
 * @brief Parse a read response (0x03/0x04) ADU into register values.
 * @param[in]  adu        Response bytes.
 * @param[in]  len        Response length.
 * @param[in]  slave      Expected unit id.
 * @param[in]  func       Expected function code.
 * @param[out] out        Register values (big-endian per Modbus).
 * @param[in]  max        Capacity of @p out.
 * @param[out] out_count  Registers written (may be NULL).
 * @param[out] out_exc    Exception code if the reply was an error (may be NULL).
 * @return ESP_OK; ESP_ERR_INVALID_CRC; ESP_ERR_INVALID_RESPONSE (wrong slave/func
 *         or an exception — see @p out_exc); or ESP_ERR_INVALID_SIZE.
 */
esp_err_t e32p_modbus_parse_read_response(const uint8_t *adu, size_t len, uint8_t slave, uint8_t func,
                                          uint16_t *out, size_t max, size_t *out_count, uint8_t *out_exc);

/* ---- Layer 2: pure slave request handler --------------------------------- */

/** @brief A minimal slave register model. `holding` is read/write; `input` is
 *         read-only. Either bank may be NULL/0 if unused. */
typedef struct {
    uint8_t   address;      /*!< This slave's unit id (1..247). */
    uint16_t *holding;      /*!< Holding registers (0x03 read, 0x06/0x10 write). */
    size_t    n_holding;
    const uint16_t *input;  /*!< Input registers (0x04 read-only). */
    size_t    n_input;
} e32p_modbus_slave_t;

/**
 * @brief Process one request ADU against @p slave and build a response ADU.
 * @param[in]  slave  Register model (its `holding` bank may be written).
 * @param[in]  req    Request ADU (CRC is verified).
 * @param[in]  req_len Request length.
 * @param[out] resp   Response ADU buffer.
 * @param[in]  cap    Capacity of @p resp.
 * @return Response length (>0); 0 if the request is not for us or is a broadcast
 *         (no reply is sent); -1 on a bad CRC / malformed request / small buffer.
 */
int e32p_modbus_slave_process(e32p_modbus_slave_t *slave, const uint8_t *req, size_t req_len,
                              uint8_t *resp, size_t cap);

/* ---- Layer 3: master over UART ------------------------------------------- */

/** @brief Master configuration. */
typedef struct {
    int                  uart_port;           /*!< 1 or 2. */
    int                  tx_pin;
    int                  rx_pin;
    uint32_t             baud;                 /*!< e.g. 9600, 19200. */
    e32p_uart_parity_t   parity;              /*!< Modbus RTU is commonly EVEN (8E1). */
    e32p_uart_stopbits_t stop_bits;
    uint32_t             response_timeout_ms; /*!< Per-transaction reply timeout. */
} e32p_modbus_master_config_t;

/** @brief Defaults: 9600 8E1, 500 ms timeout. */
#define E32P_MODBUS_MASTER_CONFIG_DEFAULT() ((e32p_modbus_master_config_t){ \
    .uart_port = 1, .tx_pin = 17, .rx_pin = 16, .baud = 9600,               \
    .parity = E32P_UART_PARITY_EVEN, .stop_bits = E32P_UART_STOP_1,         \
    .response_timeout_ms = 500 })

/** @brief Opaque master handle. */
typedef struct e32p_modbus_master_ctx *e32p_modbus_master_handle_t;

/** @brief Open a master on a UART port. */
esp_err_t e32p_modbus_master_init(const e32p_modbus_master_config_t *cfg, e32p_modbus_master_handle_t *out);

/** @brief Close the master and release the UART (NULL is a no-op). */
esp_err_t e32p_modbus_master_deinit(e32p_modbus_master_handle_t h);

/**
 * @brief Read @p count holding registers (0x03) from @p slave into @p out.
 * @return ESP_OK; ESP_ERR_INVALID_ARG; ESP_ERR_TIMEOUT (no/short reply);
 *         ESP_ERR_INVALID_CRC; or ESP_ERR_INVALID_RESPONSE (a Modbus exception;
 *         see e32p_modbus_last_exception()).
 */
esp_err_t e32p_modbus_read_holding_registers(e32p_modbus_master_handle_t h, uint8_t slave,
                                             uint16_t addr, uint16_t count, uint16_t *out);

/** @brief Read @p count input registers (0x04). */
esp_err_t e32p_modbus_read_input_registers(e32p_modbus_master_handle_t h, uint8_t slave,
                                           uint16_t addr, uint16_t count, uint16_t *out);

/** @brief Write one holding register (0x06). */
esp_err_t e32p_modbus_write_single_register(e32p_modbus_master_handle_t h, uint8_t slave,
                                            uint16_t addr, uint16_t value);

/** @brief Write @p count holding registers (0x10). */
esp_err_t e32p_modbus_write_multiple_registers(e32p_modbus_master_handle_t h, uint8_t slave,
                                               uint16_t addr, uint16_t count, const uint16_t *values);

/** @brief Exception code from the most recent transaction (0 if none). */
uint8_t e32p_modbus_last_exception(e32p_modbus_master_handle_t h);

#ifdef __cplusplus
}
#endif
