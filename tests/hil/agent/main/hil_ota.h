#pragma once
/**
 * @file hil_ota.h
 * @brief OTA HIL self-test (single board, non-destructive): reports the running
 *        slot, next slot, app version, image size, and rollback state. A real
 *        image swap is the 25_ota demo (it reboots, so it can't run in-session).
 */
#include "hil_agent.h"

#ifdef __cplusplus
extern "C" {
#endif

hil_verdict_t hil_ota_query(char *detail, size_t n);

#ifdef __cplusplus
}
#endif
