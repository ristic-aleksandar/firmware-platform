#pragma once
/**
 * @file hil_agent.h
 * @brief HIL test-agent core: role persistence, the serial command dispatcher,
 *        the test registry, and shared helpers. One binary runs on both boards;
 *        the role (DUT or COMPANION) is read from NVS at startup.
 */
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Outcome of a registered test. */
typedef enum { HIL_PASS, HIL_FAIL } hil_verdict_t;

/** @brief A registered test: fills @p detail (<= @p n) and returns a verdict. */
typedef hil_verdict_t (*hil_test_fn)(char *detail, size_t n);

/**
 * @brief Emit one protocol line to the control UART (CRLF appended). Used for
 *        `@RSP`/`@EVT`/`@ERR` lines from anywhere in the agent (e.g. the
 *        companion peer task).
 */
/**
 * @brief Make the console output path safe to share.
 *
 * Creates the lock that serialises hil_emit() against ESP_LOG and routes logging
 * through it. Call once, before anything logs or emits; calling it again is a
 * no-op. Without it both still work, they just interleave.
 */
void hil_out_init(void);

void hil_emit(const char *fmt, ...);

/**
 * @brief Load the persisted role from NVS and emit `@EVT READY`.
 *        Call after e32p_nvs_init() and after the UART driver is installed.
 */
void hil_agent_init(void);

/** @brief True if this board is provisioned as the COMPANION. */
bool hil_agent_is_companion(void);

/**
 * @brief Parse and handle one command line (without its terminator). Emits an
 *        `@RSP`/`@ERR` response. The buffer is modified in place.
 */
void hil_agent_dispatch(char *line);

#ifdef __cplusplus
}
#endif
