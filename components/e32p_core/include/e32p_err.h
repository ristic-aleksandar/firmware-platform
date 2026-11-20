#pragma once
/**
 * @file e32p_err.h
 * @brief Platform error convention: esp_err_t passthrough + a small custom range.
 *
 * Rule: modules return esp_err_t. IDF codes are used wherever they fit. The
 * ESP_ERR_E32P_* codes below cover only conditions IDF cannot express. Every
 * internal IDF call is checked with an E32P_RETURN_ON_ERR-family macro.
 */
#include "esp_err.h"
#include "esp_check.h"   /* ESP_RETURN_ON_ERROR / ESP_RETURN_ON_FALSE / ESP_GOTO_* */

/** Base for platform-specific error codes. Chosen above IDF's reserved ranges;
 *  verify against esp_common/include/esp_err.h before release. */
#define ESP_ERR_E32P_BASE                    0x30000

#define ESP_ERR_E32P_NOT_INIT                (ESP_ERR_E32P_BASE + 1) /*!< Handle used before init */
#define ESP_ERR_E32P_ALREADY_INIT            (ESP_ERR_E32P_BASE + 2) /*!< Singleton init called twice */
#define ESP_ERR_E32P_UNSUPPORTED_ON_TARGET   (ESP_ERR_E32P_BASE + 3) /*!< Feature absent on this SoC */
#define ESP_ERR_E32P_RESOURCE_CONFLICT       (ESP_ERR_E32P_BASE + 4) /*!< e.g. ADC2 while Wi-Fi active */
#define ESP_ERR_E32P_PIN_RESERVED            (ESP_ERR_E32P_BASE + 5) /*!< Flash pins 6-11 / strapping */

/**
 * @brief Return from the current function if @p expr yields a non-ESP_OK esp_err_t,
 *        logging under @p tag. Thin wrapper over ESP_RETURN_ON_ERROR.
 * @note Logs at ERROR level on any non-OK. Do NOT wrap calls whose non-OK result
 *       is an expected/benign outcome (e.g. an NVS read that may be NOT_FOUND) —
 *       check and return those quietly instead.
 */
#define E32P_RETURN_ON_ERR(expr, tag) \
    ESP_RETURN_ON_ERROR((expr), (tag), "check failed: %s", #expr)

/**
 * @brief Return ESP_ERR_INVALID_ARG under @p tag if @p cond is false.
 */
#define E32P_CHECK_ARG(cond, tag) \
    ESP_RETURN_ON_FALSE((cond), ESP_ERR_INVALID_ARG, (tag), "invalid arg: %s", #cond)

/**
 * @brief Return @p err_code under @p tag if @p cond is false (general guard).
 * @note @p msg is a printf-style format string; extra args are forwarded.
 *       It must NOT be parenthesized — ESP_RETURN_ON_FALSE concatenates it
 *       with a literal prefix, so a parenthesized token breaks compilation.
 */
#define E32P_RETURN_ON_FALSE(cond, err_code, tag, msg, ...) \
    ESP_RETURN_ON_FALSE((cond), (err_code), (tag), msg, ##__VA_ARGS__)

/**
 * @brief goto @p label if @p expr yields a non-ESP_OK esp_err_t. Requires a local
 *        `esp_err_t ret;` and a label in scope. Wrapper over ESP_GOTO_ON_ERROR.
 */
#define E32P_GOTO_ON_ERR(expr, tag, label) \
    ESP_GOTO_ON_ERROR((expr), label, (tag), "check failed: %s", #expr)
