# e32p_gpio — digital I/O facade

Thin per-pin wrapper over the ESP-IDF `driver/gpio.h`, with WROOM-32 pin-safety
guards. Flat/stateless style: GPIO pins are global hardware, so there is no
handle and no init/deinit lifecycle — you configure a pin, use it, and reset it.

## Enable

In your project's `sdkconfig.defaults` (or menuconfig):

```
CONFIG_E32P_GPIO_ENABLE=y
CONFIG_E32P_PIN_GUARDS=y      # in the "e32p: core" menu; on by default
```

Then `REQUIRES e32p_gpio` in your `main` component.

## Public API

| Function | Purpose |
|---|---|
| `e32p_gpio_config` | Configure one pin (mode / pull / interrupt). |
| `e32p_gpio_reset` | Return a pin to its default disconnected-input state. |
| `e32p_gpio_set_level` / `e32p_gpio_get_level` | Drive / read a pin. |
| `e32p_gpio_install_isr_service` / `_uninstall_isr_service` | Shared ISR dispatch. |
| `e32p_gpio_add_isr_handler` / `_remove_isr_handler` | Per-pin ISR callback. |
| `e32p_gpio_intr_enable` / `_intr_disable` | Toggle interrupts on a pin. |

| Type | Purpose |
|---|---|
| `e32p_gpio_config_t` | Caller-owned pin configuration. |
| `E32P_GPIO_CONFIG_DEFAULT()` | Sane defaults (input, no pull, no interrupt). |
| `e32p_gpio_mode_t` / `_pull_t` / `_intr_t` | Mode / pull / interrupt selectors. |
| `e32p_gpio_isr_t` | ISR callback signature (runs in interrupt context). |

## Minimal example (blink)

```c
e32p_gpio_config_t led = E32P_GPIO_CONFIG_DEFAULT();
led.pin  = 2;
led.mode = E32P_GPIO_MODE_OUTPUT;
ESP_ERROR_CHECK(e32p_gpio_config(&led));

while (1) {
    e32p_gpio_set_level(2, 1);
    vTaskDelay(pdMS_TO_TICKS(500));
    e32p_gpio_set_level(2, 0);
    vTaskDelay(pdMS_TO_TICKS(500));
}
```

## Pin / gotcha table (ESP32 / WROOM-32)

| Pins | Detail | Guard behavior (`CONFIG_E32P_PIN_GUARDS=y`) |
|---|---|---|
| GPIO6–11 | Wired to the on-module SPI flash — never usable as I/O. | `e32p_gpio_config` returns `ESP_ERR_E32P_PIN_RESERVED`. |
| GPIO0, 2, 12, 15 | Strapping pins: boot mode / flash voltage / JTAG. | Allowed, but logs a warning. Account for boot-time state. |
| GPIO34–39 | Input-only, **no** internal pull resistor. | Output mode rejected; a pull request rejected (`ESP_ERR_INVALID_ARG`). |
| GPIO5 | Also a strapping pin (boot log timing). | Not enforced; noted here — avoid for critical boot state. |

Notes:
- On WROVER modules GPIO16/17 connect to PSRAM; not guarded here (bare WROOM-32
  has no PSRAM). If you target WROVER, avoid 16/17.
- The chip-specific rules above are compiled only for `CONFIG_IDF_TARGET_ESP32`.
  Generic validity (`GPIO_IS_VALID_GPIO` / `..._OUTPUT_GPIO`) always applies, so
  the module stays correct when ported to other targets.

## Interrupt use

`e32p_gpio_install_isr_service(0)` once, then per pin: configure with an
`intr` trigger, `e32p_gpio_add_isr_handler(pin, cb, arg)`. The callback runs in
ISR context — keep it short, mark it `IRAM_ATTR` if the ISR service was
installed with `ESP_INTR_FLAG_IRAM`, and hand work to a task via a queue.

## Tests

`test/` holds the Unity cases (output round-trip, NULL/​input-only/​flash-pin
guard paths). They build when a test-runner project lists `e32p_gpio` in
`TEST_COMPONENTS`; that runner arrives with the platform example milestone.
