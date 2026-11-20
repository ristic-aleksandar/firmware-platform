/**
 * @file e32p_pm.c
 * @brief e32p_pm implementation — thin facade over esp_sleep + rtc_io/gpio.
 *
 * Argument validation and a friendly wake-cause enum are the value added over
 * the raw esp_sleep API; the sleep entries themselves are direct passthroughs.
 */
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "e32p_pm.h"

static const char *TAG = "e32p_pm";

/* ---- Pure helpers --------------------------------------------------------- */

e32p_pm_wake_cause_t e32p_pm_wake_cause_from_esp(esp_sleep_wakeup_cause_t cause)
{
    switch (cause) {
    case ESP_SLEEP_WAKEUP_TIMER:    return E32P_PM_WAKE_TIMER;
    case ESP_SLEEP_WAKEUP_EXT0:     return E32P_PM_WAKE_EXT0;
    case ESP_SLEEP_WAKEUP_EXT1:     return E32P_PM_WAKE_EXT1;
    case ESP_SLEEP_WAKEUP_TOUCHPAD: return E32P_PM_WAKE_TOUCH;
    case ESP_SLEEP_WAKEUP_GPIO:     return E32P_PM_WAKE_GPIO;
    case ESP_SLEEP_WAKEUP_UART:     return E32P_PM_WAKE_UART;
    case ESP_SLEEP_WAKEUP_ULP:      return E32P_PM_WAKE_ULP;
    default:                        return E32P_PM_WAKE_UNKNOWN;
    }
}

const char *e32p_pm_wake_cause_str(e32p_pm_wake_cause_t cause)
{
    switch (cause) {
    case E32P_PM_WAKE_TIMER: return "timer";
    case E32P_PM_WAKE_EXT0:  return "ext0";
    case E32P_PM_WAKE_EXT1:  return "ext1";
    case E32P_PM_WAKE_TOUCH: return "touch";
    case E32P_PM_WAKE_GPIO:  return "gpio";
    case E32P_PM_WAKE_UART:  return "uart";
    case E32P_PM_WAKE_ULP:   return "ulp";
    default:                 return "unknown";
    }
}

/* ---- Queries -------------------------------------------------------------- */

e32p_pm_wake_cause_t e32p_pm_wake_cause(void)
{
    return e32p_pm_wake_cause_from_esp(esp_sleep_get_wakeup_cause());
}

bool e32p_pm_is_rtc_gpio(int gpio)
{
    return (gpio >= 0) && rtc_gpio_is_valid_gpio((gpio_num_t)gpio);
}

/* ---- Wake-source configuration -------------------------------------------- */

esp_err_t e32p_pm_enable_timer_wakeup(uint64_t us)
{
    E32P_CHECK_ARG(us > 0, TAG);
    return esp_sleep_enable_timer_wakeup(us);
}

esp_err_t e32p_pm_enable_ext0_wakeup(int rtc_gpio, int level)
{
    E32P_CHECK_ARG(level == 0 || level == 1, TAG);
    E32P_CHECK_ARG(e32p_pm_is_rtc_gpio(rtc_gpio), TAG);
    return esp_sleep_enable_ext0_wakeup((gpio_num_t)rtc_gpio, level);
}

esp_err_t e32p_pm_enable_ext1_wakeup(uint64_t pin_mask, e32p_pm_ext1_mode_t mode)
{
    E32P_CHECK_ARG(pin_mask != 0, TAG);
    return esp_sleep_enable_ext1_wakeup(pin_mask, (esp_sleep_ext1_wakeup_mode_t)mode);
}

esp_err_t e32p_pm_enable_gpio_wakeup(int gpio, int level)
{
    E32P_CHECK_ARG(level == 0 || level == 1, TAG);
    E32P_CHECK_ARG(GPIO_IS_VALID_GPIO(gpio), TAG);
    const gpio_int_type_t t = level ? GPIO_INTR_HIGH_LEVEL : GPIO_INTR_LOW_LEVEL;
    E32P_RETURN_ON_ERR(gpio_wakeup_enable((gpio_num_t)gpio, t), TAG);
    return esp_sleep_enable_gpio_wakeup();
}

esp_err_t e32p_pm_enable_touch_wakeup(void)
{
    return esp_sleep_enable_touchpad_wakeup();
}

esp_err_t e32p_pm_disable_all_wakeup(void)
{
    return esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
}

/* ---- Entering sleep ------------------------------------------------------- */

esp_err_t e32p_pm_light_sleep(void)
{
    return esp_light_sleep_start();
}

void e32p_pm_deep_sleep(void)
{
    ESP_LOGI(TAG, "entering deep sleep");
    esp_deep_sleep_start();   /* does not return */
}

void e32p_pm_deep_sleep_for(uint64_t us)
{
    ESP_LOGI(TAG, "deep sleep for %llu us", (unsigned long long)us);
    esp_deep_sleep(us);       /* arms the timer and does not return */
}
