# e32p_pm — low-power sleep & wake

A small facade over ESP-IDF's `esp_sleep` API: pick wake sources, enter light or
deep sleep, and find out why the chip woke — with argument checks and a friendly
wake-cause enum on top.

## What it does

- **Wake sources:** timer, ext0 (one RTC pin), ext1 (several RTC pins), GPIO
  (light sleep), and touch — each validated (RTC-capable pin, level, non-empty
  mask) before it reaches the driver.
- **Sleep:** light sleep (returns after wake) and deep sleep (resets on wake),
  plus a `deep_sleep_for(us)` convenience.
- **Wake cause:** a compact `e32p_pm_wake_cause_t` enum with a string helper, so
  application code doesn't switch over raw `esp_sleep` constants.

## Design

Flat/stateless — sleep is a global chip resource, so there's no handle (same
style as `e32p_gpio`). The wake-cause mapping and string are **pure** and unit
tested; light sleep is exercised for real on-target (arm a 20 ms timer, sleep,
confirm it woke on the timer); deep sleep is shown in the demo (it resets the
chip, so it can't be asserted inside a test).

## API

```c
/* pure */
e32p_pm_wake_cause_t e32p_pm_wake_cause_from_esp(esp_sleep_wakeup_cause_t cause);
const char          *e32p_pm_wake_cause_str(e32p_pm_wake_cause_t cause);

/* query */
e32p_pm_wake_cause_t e32p_pm_wake_cause(void);
bool                 e32p_pm_is_rtc_gpio(int gpio);

/* wake sources */
esp_err_t e32p_pm_enable_timer_wakeup(uint64_t us);
esp_err_t e32p_pm_enable_ext0_wakeup(int rtc_gpio, int level);
esp_err_t e32p_pm_enable_ext1_wakeup(uint64_t pin_mask, e32p_pm_ext1_mode_t mode);
esp_err_t e32p_pm_enable_gpio_wakeup(int gpio, int level);   /* light sleep */
esp_err_t e32p_pm_enable_touch_wakeup(void);
esp_err_t e32p_pm_disable_all_wakeup(void);

/* sleep */
esp_err_t e32p_pm_light_sleep(void);
void      e32p_pm_deep_sleep(void);            /* no return */
void      e32p_pm_deep_sleep_for(uint64_t us); /* no return */
```

## Usage

Deep-sleep duty cycle with an RTC-retained boot counter:

```c
RTC_DATA_ATTR static int boot_count;

void app_main(void)
{
    boot_count++;
    ESP_LOGI(TAG, "boot #%d, woke by %s", boot_count,
             e32p_pm_wake_cause_str(e32p_pm_wake_cause()));

    do_work();

    e32p_pm_deep_sleep_for(10 * 1000 * 1000);   /* sleep 10 s, then reset + repeat */
}
```

Light sleep, waking on a button (RTC pin) or a timeout:

```c
e32p_pm_enable_timer_wakeup(5 * 1000 * 1000);
e32p_pm_enable_ext0_wakeup(GPIO_NUM_33, 0);     /* active-low button */
e32p_pm_light_sleep();                           /* returns on either event */
ESP_LOGI(TAG, "woke: %s", e32p_pm_wake_cause_str(e32p_pm_wake_cause()));
```

## Notes (ESP32-WROOM-32)

- **RTC / deep-sleep pins:** GPIO 0, 2, 4, 12–15, 25–27, 32–39. `ext0`/`ext1`
  only accept these; `e32p_pm_is_rtc_gpio()` checks. GPIO 34–39 are input-only
  (no pull), so add an external pull for a reliable wake level.
- **ext0 vs ext1:** ext0 is one pin at a level; ext1 is a mask of pins with
  `ANY_HIGH` / `ALL_LOW`. ext1 can wake with the RTC peripheral powered down
  (lower current); ext0 keeps RTC peripherals on.
- **GPIO wake is light-sleep only** on the ESP32; use ext0/ext1/touch for deep
  sleep.
- Deep sleep loses RAM; keep state in `RTC_DATA_ATTR` variables (RTC slow memory).

## Cherry-pick

```
CONFIG_E32P_PM_ENABLE=y
```

Requires `e32p_core` and `esp_hw_support`; privately requires `esp_driver_gpio`.
No other e32p modules needed.
