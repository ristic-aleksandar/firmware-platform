#pragma once
/**
 * @file e32p_pm.h
 * @brief Low-power sleep facade: configure wake sources, enter light/deep sleep,
 *        and report why the chip woke.
 *
 * Flat/stateless — sleep and its wake sources are global chip resources, so there
 * is no handle (same style as e32p_gpio). The wake-cause mapping is pure and
 * unit-testable; light sleep returns and is exercised on-target; deep sleep does
 * not return (the chip resets and re-runs from the top).
 */
#include <stdint.h>
#include <stdbool.h>
#include "esp_sleep.h"
#include "e32p_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Why the chip last woke (a friendlier subset of esp_sleep causes). */
typedef enum {
    E32P_PM_WAKE_UNKNOWN = 0,  /*!< Not a sleep wake — cold boot / external reset. */
    E32P_PM_WAKE_TIMER,        /*!< RTC timer elapsed. */
    E32P_PM_WAKE_EXT0,         /*!< Single RTC GPIO at a level (deep sleep). */
    E32P_PM_WAKE_EXT1,         /*!< Multiple RTC GPIOs by mask (deep sleep). */
    E32P_PM_WAKE_TOUCH,        /*!< Touch pad. */
    E32P_PM_WAKE_GPIO,         /*!< Digital GPIO (light sleep). */
    E32P_PM_WAKE_UART,         /*!< UART activity (light sleep). */
    E32P_PM_WAKE_ULP,          /*!< ULP co-processor. */
} e32p_pm_wake_cause_t;

/** @brief ext1 wake logic across the selected RTC pins (deep sleep). */
typedef enum {
    E32P_PM_EXT1_ALL_LOW  = 0,  /*!< Wake when all selected pins are low. */
    E32P_PM_EXT1_ANY_HIGH = 1,  /*!< Wake when any selected pin is high. */
} e32p_pm_ext1_mode_t;

/* ---- Pure helpers (no hardware; unit-testable) ---------------------------- */

/** @brief Map an esp_sleep wake cause to the e32p enum. */
e32p_pm_wake_cause_t e32p_pm_wake_cause_from_esp(esp_sleep_wakeup_cause_t cause);

/** @brief Human-readable name for a wake cause (never NULL). */
const char *e32p_pm_wake_cause_str(e32p_pm_wake_cause_t cause);

/* ---- Queries -------------------------------------------------------------- */

/** @brief Why the chip woke this boot (reads esp_sleep_get_wakeup_cause()). */
e32p_pm_wake_cause_t e32p_pm_wake_cause(void);

/** @brief True if @p gpio can serve as an RTC / deep-sleep wake pin. */
bool e32p_pm_is_rtc_gpio(int gpio);

/* ---- Wake-source configuration -------------------------------------------- */

/**
 * @brief Wake from a timer after @p us microseconds.
 * @return ESP_OK, ESP_ERR_INVALID_ARG (us == 0), or a driver error.
 */
esp_err_t e32p_pm_enable_timer_wakeup(uint64_t us);

/**
 * @brief Wake from a single RTC GPIO held at @p level (deep sleep).
 * @param[in] rtc_gpio  An RTC-capable GPIO (see e32p_pm_is_rtc_gpio()).
 * @param[in] level     0 or 1 — the level that triggers the wake.
 * @return ESP_OK, ESP_ERR_INVALID_ARG (non-RTC pin / bad level), or a driver error.
 */
esp_err_t e32p_pm_enable_ext0_wakeup(int rtc_gpio, int level);

/**
 * @brief Wake from any/all of several RTC GPIOs (deep sleep).
 * @param[in] pin_mask  Bitmask of RTC GPIOs (bit N = GPIO N); must be non-zero.
 * @param[in] mode      ANY_HIGH or ALL_LOW.
 * @return ESP_OK, ESP_ERR_INVALID_ARG (empty mask), or a driver error.
 */
esp_err_t e32p_pm_enable_ext1_wakeup(uint64_t pin_mask, e32p_pm_ext1_mode_t mode);

/**
 * @brief Wake from a digital GPIO at @p level (light sleep only).
 * @return ESP_OK, ESP_ERR_INVALID_ARG (bad pin / level), or a driver error.
 */
esp_err_t e32p_pm_enable_gpio_wakeup(int gpio, int level);

/** @brief Wake from any configured touch pad (deep or light sleep). */
esp_err_t e32p_pm_enable_touch_wakeup(void);

/** @brief Clear every configured wake source. */
esp_err_t e32p_pm_disable_all_wakeup(void);

/* ---- Entering sleep ------------------------------------------------------- */

/**
 * @brief Enter light sleep; returns after a wake source fires.
 * @return ESP_OK on a normal wake, or a driver error.
 */
esp_err_t e32p_pm_light_sleep(void);

/** @brief Enter deep sleep. Does not return — the chip resets on wake. */
void e32p_pm_deep_sleep(void);

/**
 * @brief Arm a timer wake for @p us and enter deep sleep. Does not return.
 * @param[in] us  Sleep duration in microseconds.
 */
void e32p_pm_deep_sleep_for(uint64_t us);

#ifdef __cplusplus
}
#endif
