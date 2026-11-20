# e32p_pm — Design

> **Low-power sleep and wake-source facade over `esp_sleep`.** Part of the ESP32
> firmware platform (`e32p_` modules). This document describes the module's
> internals and external connections and is self-contained: it travels with the
> module when cherry-picked.

## 1. Overview

Battery-powered firmware spends most of its life asleep, waking on a timer or a
pin to do a little work and sleeping again. ESP-IDF exposes this through
`esp_sleep`, a flat global API with per-source enable calls and a single
`esp_sleep_get_wakeup_cause()`. This module wraps it with argument validation
(RTC-capable pins, non-zero timer, non-empty mask), a friendly wake-cause enum,
and clear light-vs-deep entry points.

| Property | Value |
|----------|-------|
| Registration style | flat / stateless (sleep is a global resource) |
| Kconfig symbol | `CONFIG_E32P_PM_ENABLE` |
| Target guard | none (all ESP32 variants sleep; pin sets differ) |
| Public header | [`include/e32p_pm.h`](include/e32p_pm.h) |

## 2. External dependencies

| Direction | Depends on | Why |
|-----------|-----------|-----|
| Platform | `e32p_core` | error macros, logging |
| ESP-IDF | `esp_hw_support` | `esp_sleep.h` (in the public header) + sleep entry |
| ESP-IDF | `esp_driver_gpio` | `rtc_gpio_is_valid_gpio`, `gpio_wakeup_enable` (private) |
| Kconfig `select` | none | no other e32p module required |

```mermaid
flowchart TB
    app[Application]:::ext --> mod[e32p_pm]
    mod --> core[e32p_core]
    mod --> sleep[esp_hw_support / esp_sleep]
    mod --> gpio[esp_driver_gpio / rtc_io]
    sleep --> hw[RTC controller]:::ext
    classDef ext fill:#eee,stroke:#999;
```

## 3. Internal architecture

No state of its own. Each public function is either a **pure** transform
(wake-cause enum ↔ esp constant, name string) or a **validated passthrough** to
`esp_sleep` / `rtc_io`. Validation is the only logic: RTC-pin checks for
ext0/ext1, level and mask checks, and a non-zero timer.

```mermaid
flowchart TB
    subgraph e32p_pm
        pure[wake_cause_from_esp / _str] --> map[(enum <-> esp constant)]
        query[wake_cause / is_rtc_gpio] --> pure
        enable[enable_* wake sources] --> validate[arg + RTC-pin checks]
        validate --> esp[esp_sleep_enable_*]
        enter[light_sleep / deep_sleep] --> esp2[esp_light_sleep_start / esp_deep_sleep_start]
    end
```

## 4. Key data structures

- `e32p_pm_wake_cause_t` — a compact wake-cause enum (`UNKNOWN, TIMER, EXT0, EXT1,
  TOUCH, GPIO, UART, ULP`); `UNKNOWN` also covers a cold boot / external reset.
- `e32p_pm_ext1_mode_t` — `ALL_LOW` / `ANY_HIGH`, mapped to
  `esp_sleep_ext1_wakeup_mode_t`.

## 5. Calling sequences

**Deep-sleep duty cycle**
```mermaid
sequenceDiagram
    participant app as Application
    participant mod as e32p_pm
    participant esp as esp_sleep
    app->>mod: e32p_pm_wake_cause()
    mod->>esp: esp_sleep_get_wakeup_cause()
    esp-->>mod: cause
    mod-->>app: TIMER / EXT0 / UNKNOWN…
    app->>app: do work
    app->>mod: e32p_pm_deep_sleep_for(us)
    mod->>esp: esp_deep_sleep(us)
    note over esp: chip resets on wake — no return
```

**Light sleep (returns)**
```mermaid
sequenceDiagram
    participant app as Application
    participant mod as e32p_pm
    participant esp as esp_sleep
    app->>mod: enable_timer_wakeup(us) / enable_ext0_wakeup(pin,level)
    mod->>esp: validate -> esp_sleep_enable_*()
    app->>mod: e32p_pm_light_sleep()
    mod->>esp: esp_light_sleep_start()
    esp-->>mod: (wakes) ESP_OK
    mod-->>app: ESP_OK
    app->>mod: e32p_pm_wake_cause()
```

## 7. Concurrency & context

Runs in the calling task. `esp_sleep` is global state, so wake-source config is
not per-task — treat it as a single owner (typically the app's main flow).
`e32p_pm_light_sleep()` blocks until a wake source fires; `e32p_pm_deep_sleep*()`
never returns. Do any final logging before calling deep sleep (the demo delays
briefly to flush the UART).

## 8. Error model

`esp_err_t` throughout. `E32P_CHECK_ARG` (→ `ESP_ERR_INVALID_ARG`) rejects a zero
timer, a non-RTC pin for ext0, an empty ext1 mask, an invalid GPIO, and a level
other than 0/1. Everything else is a direct passthrough of the `esp_sleep` /
`rtc_io` return code. The pure helpers cannot fail — an unrecognized esp cause
maps to `E32P_PM_WAKE_UNKNOWN` and `_str` always returns a valid string.

## 9. Cherry-pick

```
CONFIG_E32P_PM_ENABLE=y
```

Requires `e32p_core` and `esp_hw_support`; privately requires `esp_driver_gpio`.
Copy `components/e32p_pm/` (plus `e32p_core`) and enable the symbol. No other
e32p modules needed.
