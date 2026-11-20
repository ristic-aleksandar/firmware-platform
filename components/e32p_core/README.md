# e32p_core

This is the foundation component for the e32p platform. Every other e32p
module lists it in `REQUIRES`, and every module includes `e32p_core.h` to
pull in the shared conventions below.

## What it provides

**Error convention.** Modules return plain `esp_err_t`. Use IDF's own error
codes wherever they already fit (`ESP_ERR_INVALID_ARG`, `ESP_ERR_NO_MEM`,
etc.). `e32p_err.h` adds a small set of platform-specific codes for
situations IDF has no code for:

- `ESP_ERR_E32P_NOT_INIT` — a handle was used before `_init()`.
- `ESP_ERR_E32P_ALREADY_INIT` — a singleton-style init was called twice.
- `ESP_ERR_E32P_UNSUPPORTED_ON_TARGET` — the feature does not exist on this chip.
- `ESP_ERR_E32P_RESOURCE_CONFLICT` — e.g. asking for ADC2 while Wi-Fi is active.
- `ESP_ERR_E32P_PIN_RESERVED` — the requested pin is a flash pin (GPIO6-11) or a
  strapping pin (GPIO0/2/12/15) and pin guards are on.

All these codes live above IDF's own reserved error-base ranges (checked
against `esp_common/include/esp_err.h` for IDF v5.3.1), so they will never
collide with a WiFi/mesh/flash/crypto/memprot error code.

`e32p_err.h` also gives you three macros so error handling reads the same way
in every module:

- `E32P_RETURN_ON_ERR(expr, tag)` — return early if `expr` is not `ESP_OK`,
  logging under `tag`.
- `E32P_CHECK_ARG(cond, tag)` — return `ESP_ERR_INVALID_ARG` if `cond` is false.
- `E32P_GOTO_ON_ERR(expr, tag, label)` — jump to `label` on error (needs a
  local `esp_err_t ret;` and that label in scope).

These are thin wrappers over IDF's own `ESP_RETURN_ON_ERROR` / `ESP_GOTO_ON_ERROR`
from `esp_check.h`, so the behavior is exactly what you'd expect from IDF.

**Version accessor.** `e32p_version_string()` returns the platform's semantic
version string (`"0.1.0"` right now), defined in `e32p_version.h`.

**Product-build marker.** `CONFIG_E32P_PRODUCT_BUILD` (menuconfig: "e32p: core
(platform-wide)", default off) says "this build may ship". Set it only for a
build that may go to a customer. It is what integrators set. It burns nothing
and changes no runtime behaviour.

Guards do not test the marker directly. They test the hidden symbol
`CONFIG_E32P_BENCH_FORBIDDEN` (no prompt, cannot be set by hand), which is on
when `E32P_PRODUCT_BUILD`, `SECURE_BOOT` or `SECURE_FLASH_ENC_ENABLED` is on.
So the guard is fail-closed: a build with Secure Boot or Flash Encryption
counts as a product even if the marker was forgotten. `SECURE_SIGNED_APPS_*`
is deliberately not a trigger, because bench examples use signed OTA.

Bench and demo projects, and modules with bench-only options, test the derived
symbol and stop with a compile `#error` when it is set. Today that is
`tests/hil/agent`, `examples/26_ota_http` and `examples/28_secure_ota`; more
will follow, so grep for the symbol to get the current list. A new bench-only
project should add the same guard, with `sdkconfig.h` first in the include
block:

```c
#include "sdkconfig.h"
#if CONFIG_E32P_BENCH_FORBIDDEN
#error "<project> is bench/demo-only; this build is marked as a product"
#endif
```

*Setting the marker.* A product build uses its own build dir (`-B`) and its own
generated config (`-D SDKCONFIG=<file>`), with the product overlay (a file that
holds `CONFIG_E32P_PRODUCT_BUILD=y`) in `SDKCONFIG_DEFAULTS`. Two traps:

- `SDKCONFIG_DEFAULTS` replaces the project's own default list. Put the
  project's `sdkconfig.defaults` first and the overlay last.
- Write the overlay as plain ASCII with no byte-order mark. PowerShell 5.1
  `Set-Content -Encoding utf8` and `Out-File` write one, and kconfgen then
  drops line 1 with only a warning, and the build carries on as a bench build. Use
  `-Encoding ascii`, or start the overlay with a comment line.

Do not reuse a project's existing `sdkconfig`: it already contains
`# CONFIG_E32P_PRODUCT_BUILD is not set`, and that line silently overrides any
defaults overlay.

```powershell
Set-Content -Path product.defaults -Value 'CONFIG_E32P_PRODUCT_BUILD=y' -Encoding ascii
idf.py -C <project> -B <scratch_build> -D SDKCONFIG=<scratch_sdkconfig> `
  -D "SDKCONFIG_DEFAULTS=<project>/sdkconfig.defaults;<path>/product.defaults" build
```

For a non-ESP32 target, add `-D IDF_TARGET=<chip>` to the command above.

Confirm the marker took effect: `<scratch_build>/config/sdkconfig.h` must contain
both `#define CONFIG_E32P_PRODUCT_BUILD 1` and
`#define CONFIG_E32P_BENCH_FORBIDDEN 1`.

**Portability seam.** `e32p_target.h` is the one place that knows about
`CONFIG_IDF_TARGET_*`. Modules should never scatter their own
`#ifdef CONFIG_IDF_TARGET_ESP32` checks — instead they gate chip-specific
features on `SOC_*` capability macros (from `soc/soc_caps.h`) and Kconfig
`depends on SOC_*` lines. That keeps the platform portable to other targets
(ESP32-S3, C3, ...) without every module needing its own target logic.

## Tick-rate safety (`e32p_time.h`)

`pdMS_TO_TICKS(ms)` divides by the tick period, so anything shorter than one
tick becomes **0** — and a 0-tick timeout is not a short wait, it is no wait.
The loop around it busy-spins, which with idle-task watchdog checking enabled is
a panic rather than a slowdown. At `CONFIG_FREERTOS_HZ=100` that is every value
below 10 ms.

| Macro | Use for |
|---|---|
| `E32P_ASSERT_BLOCKING_MS(ms)` | A **literal** timeout that must block. Fails the build if it rounds to 0 ticks, with a message naming the fix. |
| `E32P_MS_TO_TICKS_ATLEAST1(ms)` | A **runtime or configurable** timeout — a Kconfig symbol, a struct field. Clamps to one tick. |

`e32p_time.h` is **not** pulled in by `e32p_core.h`: it needs FreeRTOS, and the
umbrella header reaches every module. Include it directly where you use it.

## The rule

Every e32p module's `CMakeLists.txt` must list `e32p_core` in `REQUIRES`.
See `_template/CMakeLists.txt` for the pattern.

## Module pattern doc

The authoritative description of how an e32p module should be structured
(handle lifecycle, config struct, Kconfig enable switch, test layout) will
live at `docs/module-pattern.md`. That file does not exist yet — for now,
`_template/` is the source of truth: copy it to start a new module.
