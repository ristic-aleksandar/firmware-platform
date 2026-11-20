#pragma once
/**
 * @file e32p_nvs.h
 * @brief Thin facade over nvs_flash. Singleton subsystem init plus a per-
 *        namespace opaque handle and one-shot convenience helpers.
 */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "e32p_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Opaque handle bound to one NVS namespace. */
typedef struct e32p_nvs_ctx *e32p_nvs_handle_t;

/** @brief Open mode for a namespace handle. */
typedef enum {
    E32P_NVS_READONLY,
    E32P_NVS_READWRITE,
} e32p_nvs_mode_t;

/**
 * @brief Initialize the default NVS partition. Auto-erases and retries once on
 *        a version/space mismatch. Idempotent.
 * @return ESP_OK or a driver error.
 */
esp_err_t e32p_nvs_init(void);

/**
 * @brief Deinitialize the default NVS partition.
 * @return ESP_OK (no-op if not initialized) or a driver error.
 */
esp_err_t e32p_nvs_deinit(void);

/**
 * @brief Test whether an error from a get/read call means "key not present".
 *        Lets callers branch on a missing key without including <nvs.h>.
 * @param[in] err  A value returned by e32p_nvs_get_* or e32p_nvs_read_*.
 * @return true if @p err is the NVS "not found" code.
 */
bool e32p_nvs_is_not_found(esp_err_t err);

/* ---- Namespace handle API (efficient: open once, batch, commit) ---- */

/**
 * @brief Open a namespace.
 * @param[in]  namespace_name  NVS namespace (<= 15 chars, must not be NULL).
 * @param[in]  mode            Read-only or read-write.
 * @param[out] out             Receives the handle on success.
 * @return ESP_OK, ESP_ERR_E32P_NOT_INIT (init not called), ESP_ERR_INVALID_ARG,
 *         ESP_ERR_NO_MEM, or a driver error.
 */
esp_err_t e32p_nvs_open(const char *namespace_name, e32p_nvs_mode_t mode,
                        e32p_nvs_handle_t *out);

/**
 * @brief Close a namespace handle (NULL is a no-op).
 * @param[in] h  Handle from e32p_nvs_open.
 * @return ESP_OK.
 */
esp_err_t e32p_nvs_close(e32p_nvs_handle_t h);

/**
 * @brief Commit pending writes on a handle to flash.
 * @param[in] h  Valid handle.
 * @return ESP_OK, ESP_ERR_INVALID_ARG, or a driver error.
 */
esp_err_t e32p_nvs_commit(e32p_nvs_handle_t h);

/** @brief Stage an int32 write (call e32p_nvs_commit to persist). */
esp_err_t e32p_nvs_set_i32(e32p_nvs_handle_t h, const char *key, int32_t value);
/** @brief Read an int32. @return ESP_ERR_NVS_NOT_FOUND if the key is absent. */
esp_err_t e32p_nvs_get_i32(e32p_nvs_handle_t h, const char *key, int32_t *out_value);
/** @brief Stage a string write (call e32p_nvs_commit to persist). */
esp_err_t e32p_nvs_set_str(e32p_nvs_handle_t h, const char *key, const char *value);
/**
 * @brief Read a string.
 * @param[in]     h          Valid handle.
 * @param[in]     key        Key name.
 * @param[out]    out        Buffer (NULL to query the required length via inout_len).
 * @param[in,out] inout_len  In: buffer size; out: bytes needed (incl. NUL).
 * @return ESP_OK, ESP_ERR_NVS_NOT_FOUND, ESP_ERR_NVS_INVALID_LENGTH, or a driver error.
 */
esp_err_t e32p_nvs_get_str(e32p_nvs_handle_t h, const char *key, char *out, size_t *inout_len);
/** @brief Erase one key (call e32p_nvs_commit to persist). */
esp_err_t e32p_nvs_erase_key(e32p_nvs_handle_t h, const char *key);

/* ---- One-shot helpers (open -> op -> [commit] -> close) ---- */

/** @brief Read a single int32 from a namespace. */
esp_err_t e32p_nvs_read_i32(const char *namespace_name, const char *key, int32_t *out_value);
/** @brief Write + commit a single int32 to a namespace. */
esp_err_t e32p_nvs_write_i32(const char *namespace_name, const char *key, int32_t value);
/** @brief Read a single string from a namespace. */
esp_err_t e32p_nvs_read_str(const char *namespace_name, const char *key, char *out, size_t *inout_len);
/** @brief Write + commit a single string to a namespace. */
esp_err_t e32p_nvs_write_str(const char *namespace_name, const char *key, const char *value);

#ifdef __cplusplus
}
#endif
