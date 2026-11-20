/**
 * @file hil_pm.c
 * @brief Low-power HIL tests. Non-disruptive: no deep sleep (that resets the
 *        board and would drop the serial session). Light sleep returns, so it
 *        is verified live here.
 */
#include <string.h>
#include <stdio.h>
#include "esp_timer.h"
#include "e32p_pm.h"
#include "hil_agent.h"
#include "hil_pm.h"

hil_verdict_t hil_pm_wakecause(char *d, size_t n)
{
    /* On a normal boot this is "unknown" (cold boot). It becomes timer/ext0/etc.
       after a deep-sleep wake. Report it; a valid string is the pass condition. */
    e32p_pm_wake_cause_t c = e32p_pm_wake_cause();
    const char *s = e32p_pm_wake_cause_str(c);
    if (s == NULL || s[0] == '\0') {
        snprintf(d, n, "empty wake-cause string");
        return HIL_FAIL;
    }
    snprintf(d, n, "boot wake cause: %s", s);
    return HIL_PASS;
}

hil_verdict_t hil_pm_lightsleep(char *d, size_t n)
{
    e32p_pm_disable_all_wakeup();
    if (e32p_pm_enable_timer_wakeup(50000) != ESP_OK) {   /* 50 ms */
        snprintf(d, n, "enable timer wakeup failed");
        return HIL_FAIL;
    }
    int64_t t0 = esp_timer_get_time();
    esp_err_t err = e32p_pm_light_sleep();
    int64_t elapsed_us = esp_timer_get_time() - t0;
    e32p_pm_wake_cause_t cause = e32p_pm_wake_cause();
    e32p_pm_disable_all_wakeup();

    if (err != ESP_OK) {
        snprintf(d, n, "light_sleep err=%s", esp_err_to_name(err));
        return HIL_FAIL;
    }
    if (cause != E32P_PM_WAKE_TIMER) {
        snprintf(d, n, "woke by %s, expected timer", e32p_pm_wake_cause_str(cause));
        return HIL_FAIL;
    }
    if (elapsed_us < 40000) {   /* should be ~50 ms */
        snprintf(d, n, "woke too early: %lld us", (long long)elapsed_us);
        return HIL_FAIL;
    }
    snprintf(d, n, "light-slept %lld us, woke on timer", (long long)elapsed_us);
    return HIL_PASS;
}
