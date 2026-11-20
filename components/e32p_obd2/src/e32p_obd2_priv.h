/**
 * @file e32p_obd2_priv.h
 * @brief Shared internals of e32p_obd2 — not part of the public API.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "e32p_obd2.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CONFIG_E32P_OBD2_MAX_DTC
#  define E32P_OBD2_MAX_DTC 10
#else
#  define E32P_OBD2_MAX_DTC CONFIG_E32P_OBD2_MAX_DTC
#endif

/** @brief One stored trouble code. */
typedef struct {
    uint16_t code;       /**< two-byte DTC, e.g. 0x0133 for P0133 */
    bool     confirmed;  /**< true: answers mode 0x03; false: mode 0x07 */
} e32p_obd2_dtc_t;

/** @brief Longest data a single PID answers with. */
#define E32P_OBD2_PID_DATA_MAX 4u

/** @brief Server instance. */
struct e32p_obd2_ctx {
    e32p_obd2_config_t cfg;
    uint8_t            pid_mask[4];          /**< mode 0x01 PID 0x00 bitmask       */
    e32p_obd2_dtc_t    dtc[E32P_OBD2_MAX_DTC];
    uint8_t            dtc_count;
    bool               freeze_valid;         /**< a freeze frame was captured      */
    uint16_t           freeze_dtc;           /**< the code that caused it          */
    uint8_t           *freeze_data;          /**< pid_count * 4 bytes, snapshot    */
};

/** @brief Load the DTC store from NVS, if a namespace is configured. */
esp_err_t e32p_obd2_dtc_load(struct e32p_obd2_ctx *c);

/** @brief Write the DTC store back to NVS, if a namespace is configured. */
esp_err_t e32p_obd2_dtc_save(struct e32p_obd2_ctx *c);

/**
 * @brief Copy the frozen value of @p pid into @p out.
 * @return true when the snapshot holds this PID
 */
bool e32p_obd2_freeze_get(const struct e32p_obd2_ctx *c, uint8_t pid, uint8_t *out, uint8_t len);

#ifdef __cplusplus
}
#endif
