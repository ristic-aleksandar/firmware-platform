#pragma once
/**
 * @file hil_pm.h
 * @brief Low-power HIL tests (single board, non-disruptive).
 *        Deep sleep resets the board, so it is left to the 22_pm demo; these
 *        cover the wake-cause report and a real light-sleep round-trip.
 */
#include "hil_agent.h"

#ifdef __cplusplus
extern "C" {
#endif

hil_verdict_t hil_pm_wakecause(char *detail, size_t n);
hil_verdict_t hil_pm_lightsleep(char *detail, size_t n);

#ifdef __cplusplus
}
#endif
