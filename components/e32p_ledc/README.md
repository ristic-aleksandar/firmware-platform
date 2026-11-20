# e32p_ledc — PWM (LED controller) facade

Opaque-handle wrapper over the ESP-IDF LEDC driver. One handle represents one
PWM channel bound to a timer. Demonstrates the platform's stateful-module
pattern: `init` → `start`/`stop` → `deinit`, with duty control and hardware fade.

## Enable

```
CONFIG_E32P_LEDC_ENABLE=y
```

Then `REQUIRES e32p_ledc` in your `main` component.

## Public API

| Function | Purpose |
|---|---|
| `e32p_ledc_init` / `e32p_ledc_deinit` | Configure a channel (left idle) / release it. |
| `e32p_ledc_start` / `e32p_ledc_stop` | Begin output at current duty / idle the pin low. |
| `e32p_ledc_set_duty` / `e32p_ledc_set_duty_percent` | Set duty raw / as 0–100 %. |
| `e32p_ledc_fade` | Hardware-fade to a target duty over N ms (starts output). |
| `e32p_ledc_get_status` | Read running / duty / max_duty / freq. |

| Type | Purpose |
|---|---|
| `e32p_ledc_config_t` | Caller-owned channel configuration. |
| `E32P_LEDC_CONFIG_DEFAULT()` | 5 kHz, 13-bit, channel 0 / timer 0, idle. |
| `e32p_ledc_status_t` | Runtime status snapshot. |

## Lifecycle

`init` configures the timer + channel then **idles** the output — a fresh handle
is not driving until you call `start`. `set_duty` while stopped just stores the
value; it is applied on the next `start`. `fade` implicitly starts output.

```c
e32p_ledc_config_t cfg = E32P_LEDC_CONFIG_DEFAULT();
cfg.pin = 18;
e32p_ledc_handle_t pwm;
ESP_ERROR_CHECK(e32p_ledc_init(&cfg, &pwm));
ESP_ERROR_CHECK(e32p_ledc_start(pwm));
ESP_ERROR_CHECK(e32p_ledc_set_duty_percent(pwm, 50));   /* half brightness */
ESP_ERROR_CHECK(e32p_ledc_fade(pwm, 0, 1000));          /* fade out over 1 s */
```

## Notes / gotchas (ESP32)

| Item | Detail |
|---|---|
| Channels / timers | 8 channels, 4 timers per speed mode (`SOC_LEDC_CHANNEL_NUM`). |
| Duty resolution | 1–20 bits on ESP32 (`SOC_LEDC_TIMER_BIT_WIDTH`). Higher bits cap max frequency. |
| Speed mode | ESP32 has high- and low-speed modes; S2/S3/C3 are low-speed only. `high_speed` falls back to low-speed automatically where unsupported. |
| Fade service | `ledc_fade_func_install` is global; `init` installs it idempotently. |
| freq × resolution | The driver rejects impossible combinations (e.g. 20-bit at high frequency) — `init` returns the driver error. |

The pin is validated with `GPIO_IS_VALID_OUTPUT_GPIO`, so input-only pins
(GPIO34–39 on ESP32) are rejected.
