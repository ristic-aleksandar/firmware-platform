/**
 * @file hil_ota.c
 * @brief OTA HIL self-test. Non-destructive: verifies the offline query layer on
 *        the agent's OTA partition table (running != next slot, plausible image
 *        size, populated app info). The actual image swap reboots the board, so
 *        it belongs to the 25_ota demo rather than this in-session harness.
 */
#include <string.h>
#include <stdio.h>
#include "e32p_ota.h"
#include "hil_agent.h"
#include "hil_ota.h"

hil_verdict_t hil_ota_query(char *d, size_t n)
{
    const char *running = e32p_ota_running_label();
    const char *next = e32p_ota_next_label();
    if (running == NULL || next == NULL || strcmp(running, next) == 0) {
        snprintf(d, n, "slots not distinct (run=%s next=%s)",
                 running ? running : "?", next ? next : "?");
        return HIL_FAIL;
    }

    e32p_ota_app_info_t info;
    if (e32p_ota_get_app_info(&info) != ESP_OK || info.project[0] == '\0') {
        snprintf(d, n, "app info missing");
        return HIL_FAIL;
    }

    size_t size = 0;
    if (e32p_ota_running_image_size(&size) != ESP_OK || size == 0 || size >= 0x180000) {
        snprintf(d, n, "implausible image size %u", (unsigned)size);
        return HIL_FAIL;
    }

    snprintf(d, n, "run=%s next=%s app=%s img=%uKB rollback=%d",
             running, next, info.project, (unsigned)(size / 1024),
             e32p_ota_rollback_possible() ? 1 : 0);
    return HIL_PASS;
}
