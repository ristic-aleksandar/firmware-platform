#pragma once
/**
 * @file hil_sys.h
 * @brief Foundation HIL tests (single board): event loop, NVS, watchdog.
 */
#include "hil_agent.h"

#ifdef __cplusplus
extern "C" {
#endif

hil_verdict_t hil_sys_event(char *detail, size_t n);
hil_verdict_t hil_sys_nvs(char *detail, size_t n);
hil_verdict_t hil_sys_wdt(char *detail, size_t n);

#ifdef __cplusplus
}
#endif
