#pragma once
/**
 * @file e32p_time.h
 * @brief Tick-rate safety for millisecond timeouts.
 *
 * `pdMS_TO_TICKS(ms)` is integer division by the tick period, so any value
 * shorter than one tick evaluates to **0** — and a 0-tick timeout is not a short
 * wait, it is *no wait at all*. A loop built around one busy-spins:
 *
 *     CONFIG_FREERTOS_HZ=100  ->  pdMS_TO_TICKS(5) == 0   (10 ms per tick)
 *     CONFIG_FREERTOS_HZ=1000 ->  pdMS_TO_TICKS(5) == 5   (1 ms per tick)
 *
 * With `CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU*` enabled, a free-running task
 * starves the idle task and panics the watchdog. The failure is silent at the
 * source: the code reads as if it waits.
 *
 * Use @ref E32P_ASSERT_BLOCKING_MS next to any *literal* timeout that must
 * block, and @ref E32P_MS_TO_TICKS_ATLEAST1 for any *runtime or configurable*
 * one. Then lowering the tick rate becomes a build error naming the fix, rather
 * than a spin that has to be found on a bench.
 *
 * Deliberately **not** included by `e32p_core.h`: it needs FreeRTOS, and the
 * umbrella header is included by every module. Include it directly where used.
 */
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Ticks for @p ms, never rounding down to 0.
 *
 * For timeouts whose value is not a compile-time constant — a Kconfig symbol, a
 * config-struct field, a computed interval. Clamps to one tick, so the caller
 * blocks for at least the tick period instead of spinning.
 *
 * @param ms Milliseconds to wait.
 * @return Tick count, minimum 1.
 */
#define E32P_MS_TO_TICKS_ATLEAST1(ms) \
    (pdMS_TO_TICKS(ms) > 0 ? pdMS_TO_TICKS(ms) : (TickType_t)1)

/**
 * @brief Build-time guard: fail the build if @p ms rounds to 0 ticks at the
 *        configured `CONFIG_FREERTOS_HZ`.
 *
 * Place beside a literal timeout whose purpose is to block. Changing the tick
 * rate then surfaces as a compiler error naming the offending value, instead of
 * silently converting the wait into a busy-spin.
 *
 * @param ms Literal millisecond value. Must be a compile-time constant.
 */
#define E32P_ASSERT_BLOCKING_MS(ms)                                            \
    _Static_assert(pdMS_TO_TICKS(ms) >= 1,                                     \
        "timeout " #ms " ms rounds to 0 ticks at this CONFIG_FREERTOS_HZ - "   \
        "it would busy-spin, not wait; raise the value, raise the tick rate, " \
        "or use E32P_MS_TO_TICKS_ATLEAST1()")

#ifdef __cplusplus
}
#endif
