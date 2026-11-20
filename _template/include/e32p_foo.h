#pragma once
/**
 * @file e32p_foo.h
 * @brief TEMPLATE module. Copy the _template/ folder into components/ as
 *        components/e32p_<name>/, then rename foo -> <name>.
 *        Demonstrates the canonical opaque-handle lifecycle.
 */
#include <stdbool.h>     /* public struct uses bool; IDF builds gnu17 (not a keyword) */
#include "e32p_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Opaque instance handle. */
typedef struct e32p_foo_ctx *e32p_foo_handle_t;

/** @brief Configuration (caller-owned, passed by const pointer). */
typedef struct {
    int   example_pin;     /*!< Replace with real config fields. */
    bool  example_flag;    /*!< Replace with real config fields. */
} e32p_foo_config_t;

/** @brief Sane defaults; override only what you need. */
#define E32P_FOO_CONFIG_DEFAULT()  ((e32p_foo_config_t){ \
    .example_pin  = -1,                                  \
    .example_flag = false,                               \
})

/** @brief Runtime status snapshot. */
typedef struct {
    bool  running;         /*!< True between _start and _stop. */
} e32p_foo_status_t;

/**
 * @brief Initialize an instance.
 * @param[in]  cfg         Configuration (must not be NULL).
 * @param[out] out_handle  Receives the new handle on success.
 * @return ESP_OK, ESP_ERR_INVALID_ARG, ESP_ERR_NO_MEM, or a driver error.
 */
esp_err_t e32p_foo_init(const e32p_foo_config_t *cfg, e32p_foo_handle_t *out_handle);

/**
 * @brief Release an instance and its resources. Do not reuse @p h afterwards.
 * @param[in] h  Handle from e32p_foo_init. NULL is a no-op returning ESP_OK;
 *               passing an already-freed handle is undefined (double free).
 * @return ESP_OK or a driver error.
 */
esp_err_t e32p_foo_deinit(e32p_foo_handle_t h);

/**
 * @brief Start operation.
 * @param[in] h  Valid handle.
 * @return ESP_OK, ESP_ERR_INVALID_ARG (NULL handle), or a driver error.
 */
esp_err_t e32p_foo_start(e32p_foo_handle_t h);

/**
 * @brief Stop operation.
 * @param[in] h  Valid handle.
 * @return ESP_OK or a driver error.
 */
esp_err_t e32p_foo_stop(e32p_foo_handle_t h);

/**
 * @brief Read current status.
 * @param[in]  h    Valid handle.
 * @param[out] out  Receives the status snapshot.
 * @return ESP_OK or ESP_ERR_INVALID_ARG.
 */
esp_err_t e32p_foo_get_status(e32p_foo_handle_t h, e32p_foo_status_t *out);

#ifdef __cplusplus
}
#endif
