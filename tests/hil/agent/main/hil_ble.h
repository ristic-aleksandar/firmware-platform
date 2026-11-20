#pragma once
/**
 * @file hil_ble.h
 * @brief BLE HIL self-test (single board, no radio): verifies the pure
 *        advertising-payload assembly + UUID helpers on-target. The real
 *        peripheral (advertise + a central connecting) is the 27_ble demo, which
 *        needs the Bluetooth host + a phone; the agent stays BT-free.
 */
#include "hil_agent.h"

#ifdef __cplusplus
extern "C" {
#endif

hil_verdict_t hil_ble_selftest(char *detail, size_t n);

#ifdef __cplusplus
}
#endif
