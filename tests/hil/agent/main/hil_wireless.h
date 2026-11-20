#pragma once
/**
 * @file hil_wireless.h
 * @brief Wi-Fi / ESP-NOW HIL tests. Arg-validation runs single board; the
 *        ESP-NOW transfer needs a second board (the ESP-NOW peer companion).
 */
#include "hil_agent.h"

#ifdef __cplusplus
extern "C" {
#endif

hil_verdict_t hil_wl_wifi_neg(char *detail, size_t n);      /* 1 board (arg validation) */
hil_verdict_t hil_wl_espnow_neg(char *detail, size_t n);    /* 1 board (arg validation) */
hil_verdict_t hil_wl_espnow_xfer(char *detail, size_t n);   /* 2 board (companion peer) */

/** @brief Start the ESP-NOW peer (COMPANION): receives + logs `@EVT ESPNOW`. */
void hil_wireless_start_companion(void);

#ifdef __cplusplus
}
#endif
