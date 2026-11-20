# 00_blink_gpio

The platform's first example. Blinks an LED on **GPIO2** using `e32p_gpio`, and
logs the platform version from `e32p_core`. It exists to prove the M0 slice
(core + GPIO) builds, links, and runs on a real ESP-WROOM-32.

## What it demonstrates

- Cherry-picking modules: `EXTRA_COMPONENT_DIRS` points at `../../components`
  and only `CONFIG_E32P_GPIO_ENABLE=y` is set, so just `e32p_core` + `e32p_gpio`
  compile in.
- The canonical config-struct flow: `E32P_GPIO_CONFIG_DEFAULT()` → set fields →
  `e32p_gpio_config()`.

## Build & flash

From the workspace, use the VS Code task **ESP-IDF: build** and point it at this
folder, or by hand:

```powershell
. C:\Espressif\Initialize-Idf.ps1 -IdfId esp-idf-ab7213b7273352b64422b1f400ff27a0
idf.py -C C:\Users\aleks\Work\esp32_platform\examples\00_blink_gpio set-target esp32
idf.py -C C:\Users\aleks\Work\esp32_platform\examples\00_blink_gpio -p COM5 build flash monitor
```

Exit the monitor with **Ctrl+]**. Expected log line at boot:

```
I (…) blink: esp32_platform 0.1.0 - blink on GPIO2
```

## Wiring note

GPIO2 drives the on-board LED on many DevKits, but not all WROOM-32 boards. If
yours differs, change `LED_PIN` in `main/main.c`. GPIO2 is also a strapping pin,
so `e32p_gpio` logs a warning when configuring it — that is expected here.
