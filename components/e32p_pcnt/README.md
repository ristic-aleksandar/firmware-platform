# e32p_pcnt — pulse counter facade

Facade over the ESP-IDF v5.x pulse counter (PCNT). One handle = one PCNT unit
counting rising/falling edges on a GPIO.

## Enable

```
CONFIG_E32P_PCNT_ENABLE=y
```
Then `REQUIRES e32p_pcnt`. (Hidden on SoCs without PCNT.)

## API

| Function | Purpose |
|---|---|
| `e32p_pcnt_init` / `_deinit` | Create a unit + edge channel (left stopped) / release. |
| `e32p_pcnt_start` / `_stop` | Run / pause counting. |
| `e32p_pcnt_clear` | Reset the count to 0. |
| `e32p_pcnt_get_count` | Read the signed count. |

## Example (count an external signal)

```c
e32p_pcnt_config_t cfg = { .edge_gpio = 4, .high_limit = 10000, .low_limit = -10000,
                           .count_rising = true, .count_falling = false };
e32p_pcnt_handle_t pcnt;
ESP_ERROR_CHECK(e32p_pcnt_init(&cfg, &pcnt));
ESP_ERROR_CHECK(e32p_pcnt_start(pcnt));
/* ... later ... */
int n; e32p_pcnt_get_count(pcnt, &n);
```

## Notes / gotchas

| Item | Detail |
|---|---|
| Range | ESP32 counter is 16-bit: `high_limit` ≤ 32767, `low_limit` ≥ -32768. Crossing a limit resets to 0 (watch-point). |
| Self-test | Set `io_loop_back = true`, then `gpio_set_level(edge_gpio, 1/0)` generates countable edges with no wiring — see `examples/11_pcnt_count`. |
| Edges | Choose `count_rising` and/or `count_falling`. For a quadrature encoder, add the second signal as the level input (a future extension). |
| Glitches | For noisy mechanical inputs, a glitch filter can be added later; this facade currently leaves it off. |
