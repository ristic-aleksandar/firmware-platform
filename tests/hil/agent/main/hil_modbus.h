#pragma once
/**
 * @file hil_modbus.h
 * @brief Modbus HIL tests: a single-board self-test (pure protocol round-trip)
 *        and a two-board RTU transport test (DUT master <-> COMPANION slave over
 *        a crossed UART2 link — see tests/hil/WIRING.md §8.3).
 */
#include "hil_agent.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Single board, no wire: CRC vector + pure master/slave round-trip. */
hil_verdict_t hil_modbus_selftest(char *detail, size_t n);

/** @brief Two boards: real UART RTU master transaction against the companion slave. */
hil_verdict_t hil_modbus_rtu_xfer(char *detail, size_t n);

/** @brief Start the Modbus-slave peer (COMPANION only). */
void hil_modbus_start_companion(void);

#ifdef __cplusplus
}
#endif
