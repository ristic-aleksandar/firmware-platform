# e32p_sysinfo — system introspection

Read-only helpers for chip/app identity, reset reason, uptime, and heap. No
init — every function just reads current state.

## Enable

```
CONFIG_E32P_SYSINFO_ENABLE=y
```
Then `REQUIRES e32p_sysinfo`.

## API

| Function | Purpose |
|---|---|
| `e32p_sysinfo_get` | Fill an `e32p_sysinfo_t` (chip, cores, rev, flash, MAC, IDF/app versions). |
| `e32p_sysinfo_reset_reason` / `_str` | Last reset cause as an enum / human string. |
| `e32p_sysinfo_uptime_ms` | Milliseconds since boot. |
| `e32p_sysinfo_free_heap` / `_min_free_heap` | Current / lowest-ever free heap. |
| `e32p_sysinfo_log` | One-call summary dump at INFO. |

## Example

```c
e32p_sysinfo_log();
ESP_LOGI(TAG, "came up because: %s", e32p_sysinfo_reset_reason_str());
```

## Notes

- The reset reason is stable for the whole boot — read it once at startup. It's
  the cleanest way to tell a crash (`panic` / `task watchdog`) from a normal
  power-on or software restart.
- `e32p_reset_reason_t` is a portable enum mapped from `esp_reset_reason()`, so
  app code needn't include `esp_system.h`.
