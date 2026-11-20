# e32p_touch — capacitive touch facade

Thin facade over the ESP32 touch sensor: init, configure a pad, and read its raw
capacitance value. Detecting a "touch" (compare to a baseline) is left to the app.

## Enable

```
CONFIG_E32P_TOUCH_ENABLE=y
```
Then `REQUIRES e32p_touch`. (Hidden on SoCs without touch, e.g. C3.)

## API

| Function | Purpose |
|---|---|
| `e32p_touch_init` / `_deinit` | Start / stop the touch subsystem (idempotent init). |
| `e32p_touch_config_pad` | Enable a touch channel (0..9). |
| `e32p_touch_read` | Read a pad's raw value. |

## Touch channel → GPIO (ESP32)

| T | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 |
|---|---|---|---|---|---|---|---|---|---|---|
| GPIO | 4 | 0 | 2 | 15 | 13 | 12 | 14 | 27 | 33 | 32 |

## Example (detect a touch)

```c
ESP_ERROR_CHECK(e32p_touch_init());
ESP_ERROR_CHECK(e32p_touch_config_pad(0));         /* T0 = GPIO4 */

uint32_t baseline;  e32p_touch_read(0, &baseline);  /* untouched reference */
uint32_t v;         e32p_touch_read(0, &v);
bool touched = (v < baseline * 4 / 5);              /* value drops when touched */
```

## Notes / gotchas

| Item | Detail |
|---|---|
| Polarity | On ESP32 (touch v1) the raw value **decreases** when touched. |
| Baseline | Sample an untouched baseline at startup and compare — absolute values drift with humidity/temperature. |
| Strapping pins | Several touch pads are strapping pins (T1=GPIO0, T2=GPIO2, T3=GPIO15, T5=GPIO12). T0 (GPIO4) is a safe default. |
| Component | The ESP32 touch driver is the one peripheral still in the legacy `driver` component (not an `esp_driver_*`). |
