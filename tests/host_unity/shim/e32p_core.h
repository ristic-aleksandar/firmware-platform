/**
 * @file e32p_core.h  (host stand-in)
 * @brief Just enough of e32p_core for pure component code to build on the PC.
 *
 * Error codes carry ESP-IDF's values. The check macros return the same errors as
 * the real ones but do not log, and they reference the tag so a module's TAG
 * counts as used under -Wall. Add a definition only when a host-built source needs
 * it: anything that touches hardware belongs on the target's unit runner, not here.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef int esp_err_t;

#define ESP_OK                0
#define ESP_FAIL              (-1)
#define ESP_ERR_NO_MEM        0x101
#define ESP_ERR_INVALID_ARG   0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_INVALID_SIZE  0x104
#define ESP_ERR_NOT_FOUND     0x105
#define ESP_ERR_NOT_SUPPORTED 0x106
#define ESP_ERR_TIMEOUT       0x107

#define ESP_LOGE(tag, ...) ((void)(tag))
#define ESP_LOGW(tag, ...) ((void)(tag))
#define ESP_LOGI(tag, ...) ((void)(tag))
#define ESP_LOGD(tag, ...) ((void)(tag))
#define ESP_LOGV(tag, ...) ((void)(tag))

#define E32P_CHECK_ARG(cond, tag)                                                     \
    do {                                                                              \
        (void)(tag);                                                                  \
        if (!(cond)) {                                                                \
            return ESP_ERR_INVALID_ARG;                                               \
        }                                                                             \
    } while (0)

#define E32P_RETURN_ON_FALSE(cond, err_code, tag, ...)                                \
    do {                                                                              \
        (void)(tag);                                                                  \
        if (!(cond)) {                                                                \
            return (err_code);                                                        \
        }                                                                             \
    } while (0)

#define E32P_RETURN_ON_ERR(expr, tag)                                                 \
    do {                                                                              \
        (void)(tag);                                                                  \
        esp_err_t e32p_err_ = (expr);                                                 \
        if (e32p_err_ != ESP_OK) {                                                    \
            return e32p_err_;                                                         \
        }                                                                             \
    } while (0)
