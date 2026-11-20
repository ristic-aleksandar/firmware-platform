#pragma once
/**
 * @file e32p_sysinfo.h
 * @brief Read-only system introspection: chip/app identity, reset reason, heap,
 *        uptime. No init required — all functions just read.
 */
#include <stdint.h>
#include "e32p_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Portable reset-cause enum (mapped from esp_reset_reason()). */
typedef enum {
    E32P_RESET_UNKNOWN,
    E32P_RESET_POWERON,     /*!< Power-on / brown-out recovery. */
    E32P_RESET_SW,          /*!< Software restart (esp_restart). */
    E32P_RESET_PANIC,       /*!< Crash / abort. */
    E32P_RESET_INT_WDT,     /*!< Interrupt watchdog. */
    E32P_RESET_TASK_WDT,    /*!< Task watchdog. */
    E32P_RESET_BROWNOUT,    /*!< Brown-out detector. */
    E32P_RESET_DEEPSLEEP,   /*!< Wake from deep sleep. */
    E32P_RESET_OTHER,       /*!< Any other cause. */
} e32p_reset_reason_t;

/** @brief Chip and application identity snapshot. */
typedef struct {
    char     chip_model[16];   /*!< e.g. "ESP32". */
    int      cores;            /*!< CPU core count. */
    int      revision;         /*!< Silicon revision, encoded major*100 + minor (100 = v1.0). */
    uint32_t flash_size_bytes; /*!< Detected flash size. */
    uint8_t  mac[6];           /*!< Factory (default) MAC. */
    char     idf_version[32];  /*!< ESP-IDF version string. */
    char     app_name[32];     /*!< Project name. */
    char     app_version[32];  /*!< App version string. */
} e32p_sysinfo_t;

/**
 * @brief Fill an identity snapshot.
 * @param[out] out  Destination (must not be NULL).
 * @return ESP_OK or ESP_ERR_INVALID_ARG.
 */
esp_err_t e32p_sysinfo_get(e32p_sysinfo_t *out);

/** @brief Reason for the last reset, as a portable enum. */
e32p_reset_reason_t e32p_sysinfo_reset_reason(void);
/** @brief Reason for the last reset, as a human string ("power-on", ...). */
const char *e32p_sysinfo_reset_reason_str(void);

/** @brief Milliseconds since boot (from the high-res timer). */
uint64_t e32p_sysinfo_uptime_ms(void);
/** @brief Current free heap in bytes. */
uint32_t e32p_sysinfo_free_heap(void);
/** @brief Lowest free-heap watermark since boot, in bytes. */
uint32_t e32p_sysinfo_min_free_heap(void);

/** @brief Log a one-call summary (chip, MAC, app, reset reason, heap) at INFO. */
void e32p_sysinfo_log(void);

#ifdef __cplusplus
}
#endif
