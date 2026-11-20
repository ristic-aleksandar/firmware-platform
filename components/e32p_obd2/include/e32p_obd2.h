/**
 * @file e32p_obd2.h
 * @brief SAE J1979 / ISO 15765-4 OBD-II services — pure, clockless service layer.
 *
 * One request in, one response out. The layer owns no transport, no task and no
 * clock, so every service is testable on the Unity runner with no CAN bus. The
 * transport glue (slice 2.3) feeds it reassembled ISO-TP requests and sends what
 * it produces.
 *
 * Services: 0x01 current data, 0x02 freeze frame, 0x03 stored DTCs, 0x04 clear,
 * 0x07 pending DTCs, 0x09 vehicle information.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "e32p_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Longest response this layer ever builds.
 *
 * The trouble-code list is the long one: `0x43`, a count, then two bytes per
 * code — 66 bytes at the largest store `E32P_OBD2_MAX_DTC` allows (32). The VIN
 * response is 20. One fixed number keeps the caller's buffer simple, and
 * `e32p_obd2_request()` refuses anything smaller rather than truncating a
 * response onto the bus.
 */
#define E32P_OBD2_RESP_MAX 68u

/** @brief VIN length in characters, fixed by the standard. */
#define E32P_OBD2_VIN_LEN  17u

/**
 * @brief How the tester addressed the request.
 *
 * It changes what an unsupported request answers: a physical request gets a
 * negative response, a functional one gets silence, so that a broadcast to many
 * ECUs is not answered by every ECU with an error (ISO 15765-4).
 */
typedef enum {
    E32P_OBD2_ADDR_PHYSICAL = 0,   /**< sent to this ECU alone (0x7E0)  */
    E32P_OBD2_ADDR_FUNCTIONAL,     /**< broadcast to every ECU (0x7DF)  */
} e32p_obd2_addressing_t;

/**
 * @brief One entry of the mode 0x01 PID table.
 *
 * Multiples of 0x20 (`0x00`, `0x20`, `0x40`, …) must not appear: those are the
 * supported-PID bitmasks, and the layer derives them from this table so the two
 * can never disagree. `0x1C` (OBD standard) is answered from
 * @ref e32p_obd2_config_t::obd_standard, not from the value provider.
 */
typedef struct {
    uint8_t pid;   /**< PID number, e.g. 0x0C for engine RPM        */
    uint8_t len;   /**< data bytes this PID answers with (1..4)     */
} e32p_obd2_pid_t;

/**
 * @brief Fill @p out with the current value of @p pid.
 *
 * Called while a request is being answered, in the caller's context. Keep it
 * short: read a cached value, do not block.
 *
 * @param[in]  pid   PID being read
 * @param[out] out   buffer of exactly @p len bytes, already zeroed
 * @param[in]  len   the table's byte count for this PID
 * @param[in]  user  @ref e32p_obd2_config_t::user
 * @return ESP_OK when @p out holds a value; anything else answers as unsupported
 */
typedef esp_err_t (*e32p_obd2_read_fn)(uint8_t pid, uint8_t *out, uint8_t len, void *user);

/** @brief Server configuration. The table and the VIN must outlive the handle. */
typedef struct {
    const e32p_obd2_pid_t *pids;          /**< mode 0x01 table, ascending, without 0x00 / 0x20 */
    uint8_t                pid_count;     /**< entries in @ref pids                            */
    e32p_obd2_read_fn      on_read;       /**< supplies live values                            */
    void                  *user;          /**< passed back to @ref on_read                     */
    const char            *vin;           /**< 17 chars, or NULL: mode 0x09 PID 0x02 unsupported */
    uint8_t                obd_standard;  /**< value for PID 0x1C, e.g. 0x06 = EOBD            */
    const char            *nvs_namespace; /**< DTC store namespace, or NULL to keep codes in RAM */
} e32p_obd2_config_t;

/** @brief Opaque server instance. */
typedef struct e32p_obd2_ctx *e32p_obd2_handle_t;

/**
 * @brief Create a server.
 *
 * Reads the DTC store from NVS when @ref e32p_obd2_config_t::nvs_namespace is set,
 * and computes the supported-PID bitmasks from the table, so they can never
 * disagree with it.
 *
 * @param[in]  cfg  configuration, copied
 * @param[out] out  handle
 * @return ESP_OK, ESP_ERR_INVALID_ARG, or ESP_ERR_NO_MEM
 */
esp_err_t e32p_obd2_init(const e32p_obd2_config_t *cfg, e32p_obd2_handle_t *out);

/** @brief Destroy a server. NULL is a no-op. */
esp_err_t e32p_obd2_deinit(e32p_obd2_handle_t h);

/**
 * @brief Answer one request.
 *
 * @param[in]  h           server
 * @param[in]  addressing  how the tester addressed it
 * @param[in]  req         request bytes, starting at the service id
 * @param[in]  req_len     length of @p req
 * @param[out] resp        response buffer, at least @ref E32P_OBD2_RESP_MAX
 * @param[in]  resp_cap    capacity of @p resp
 * @param[out] resp_len    bytes written; **0 means send nothing** (see
 *                         @ref e32p_obd2_addressing_t)
 * @return ESP_OK when the request was handled, ESP_ERR_INVALID_ARG on bad
 *         arguments. A negative response is still ESP_OK: it is a response.
 */
esp_err_t e32p_obd2_request(e32p_obd2_handle_t h, e32p_obd2_addressing_t addressing,
                            const uint8_t *req, uint16_t req_len,
                            uint8_t *resp, uint16_t resp_cap, uint16_t *resp_len);

/**
 * @brief Store a trouble code, e.g. 0x0133 for P0133.
 *
 * Confirmed codes answer mode 0x03, pending ones mode 0x07. The first code
 * stored also captures the freeze frame. Persists to NVS when configured.
 *
 * @return ESP_OK, ESP_ERR_NO_MEM when the store is full, ESP_ERR_INVALID_ARG
 */
esp_err_t e32p_obd2_dtc_set(e32p_obd2_handle_t h, uint16_t dtc, bool confirmed);

/** @brief Erase stored codes, pending codes and the freeze frame — mode 0x04. */
esp_err_t e32p_obd2_dtc_clear(e32p_obd2_handle_t h);

/** @brief How many codes are stored, confirmed (@p confirmed true) or pending. */
uint8_t e32p_obd2_dtc_count(e32p_obd2_handle_t h, bool confirmed);

/**
 * @brief Read the @p index-th stored code, confirmed or pending alike — so another
 *        view of the same store (UDS 0x19) can list it.
 *
 * @param[in]  index      0 .. number of codes stored - 1, in the order they were set
 * @param[out] dtc        the code, e.g. 0x0133
 * @param[out] confirmed  true for a confirmed code, false for a pending one
 * @return ESP_OK, ESP_ERR_NOT_FOUND past the last code, ESP_ERR_INVALID_ARG
 */
esp_err_t e32p_obd2_dtc_get(e32p_obd2_handle_t h, uint8_t index, uint16_t *dtc, bool *confirmed);

#ifdef __cplusplus
}
#endif
