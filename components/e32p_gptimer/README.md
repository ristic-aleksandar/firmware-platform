# e32p_gptimer — general-purpose timer facade

Facade over the ESP-IDF general-purpose hardware timer: a periodic or one-shot
alarm with an ISR callback, plus a raw-count read. One handle per timer.

## Enable

```
CONFIG_E32P_GPTIMER_ENABLE=y
```
Then `REQUIRES e32p_gptimer`.

## API

| Function | Purpose |
|---|---|
| `e32p_gptimer_init` | Create a timer + alarm (left stopped); register an ISR callback. |
| `e32p_gptimer_deinit` | Stop and release. |
| `e32p_gptimer_start` / `_stop` | Run / pause. |
| `e32p_gptimer_get_count` | Read the raw tick count. |

## Example (1 Hz alarm)

```c
static volatile uint32_t ticks;
static void on_tick(void *arg) { ticks++; }          /* ISR context */

e32p_gptimer_config_t cfg = { .resolution_hz = 1000000,     /* 1 us/tick */
                              .alarm_period_ticks = 1000000, /* 1 s */
                              .auto_reload = true };
e32p_gptimer_handle_t t;
ESP_ERROR_CHECK(e32p_gptimer_init(&cfg, on_tick, NULL, &t));
ESP_ERROR_CHECK(e32p_gptimer_start(t));
```

## Notes / gotchas

| Item | Detail |
|---|---|
| ISR context | The callback runs in an interrupt. Keep it short; set a flag or give a semaphore and do the work in a task. Mark it `IRAM_ATTR` if it must run while flash cache is disabled. |
| Resolution | `resolution_hz` sets tick rate. 1 MHz gives 1 µs ticks — a good general default. |
| Periodic vs one-shot | `auto_reload = true` re-arms after each alarm; `false` fires once. |
| Timer count | ESP32 has a limited number of hardware timer units — deinit ones you no longer need. |
