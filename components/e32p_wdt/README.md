# e32p_wdt — task watchdog facade

Thin wrapper over the ESP-IDF Task Watchdog Timer (TWDT). Configure the timeout,
subscribe a task, and feed it; if a watched task stops feeding within the
timeout, the WDT warns (or panics + reboots).

## Enable

```
CONFIG_E32P_WDT_ENABLE=y
```
Then `REQUIRES e32p_wdt`.

## API

| Function | Purpose |
|---|---|
| `e32p_wdt_init` | Set timeout + panic mode (idempotent; reconfigures a system-started TWDT). |
| `e32p_wdt_deinit` | Tear down the TWDT (unwatch tasks first). |
| `e32p_wdt_watch_current_task` / `_unwatch_current_task` | Subscribe / unsubscribe the caller. |
| `e32p_wdt_feed` | Reset the watchdog for the calling task. |

## Example

```c
ESP_ERROR_CHECK(e32p_wdt_init(3000, true));   /* 3 s, panic on timeout */
ESP_ERROR_CHECK(e32p_wdt_watch_current_task());
while (1) {
    do_work();
    ESP_ERROR_CHECK(e32p_wdt_feed());          /* must run within 3 s */
    vTaskDelay(pdMS_TO_TICKS(1000));
}
```

## Notes

- Each **watched task must feed itself** within the timeout. A blocked or
  looping task that stops feeding triggers the WDT — on the next boot,
  `e32p_sysinfo_reset_reason_str()` reports `task watchdog`.
- `init` uses `idle_core_mask = 0` (it watches only the tasks you subscribe).
- Keep `panic_on_timeout = true` in production so a hung task reboots the device
  rather than hanging silently.
