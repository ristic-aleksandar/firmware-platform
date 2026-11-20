# e32p_adc — oneshot ADC facade

Oneshot ADC over the ESP-IDF v5.x `esp_adc` driver, with per-channel attenuation
and optional line-fitting calibration to millivolts. One handle per ADC unit.

## Enable

```
CONFIG_E32P_ADC_ENABLE=y
```
Then `REQUIRES e32p_adc`.

## API

| Function | Purpose |
|---|---|
| `e32p_adc_init` / `_deinit` | Open / release an ADC unit (1 or 2). |
| `e32p_adc_config_channel` | Set a channel's attenuation; sets up calibration. |
| `e32p_adc_read_raw` | Read a 0..4095 count. |
| `e32p_adc_read_voltage` | Read calibrated millivolts (after config_channel). |

## Example

```c
e32p_adc_handle_t adc;
ESP_ERROR_CHECK(e32p_adc_init(1, &adc));
ESP_ERROR_CHECK(e32p_adc_config_channel(adc, 6, E32P_ADC_ATTEN_12DB));   /* GPIO34 */

int raw, mv;
ESP_ERROR_CHECK(e32p_adc_read_raw(adc, 6, &raw));
e32p_adc_read_voltage(adc, 6, &mv);     /* mV if the chip has calibration data */
```

## ADC1 channel → GPIO (ESP32)

| CH | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|----|---|---|---|---|---|---|---|---|
| GPIO | 36 | 37 | 38 | 39 | 32 | 33 | 34 | 35 |

ADC2 channels map to GPIO 4/0/2/15/13/12/14/27/25/26 (CH0..CH9).

## Notes / gotchas

| Item | Detail |
|---|---|
| **ADC2 vs Wi-Fi** | ADC2 shares hardware with the Wi-Fi radio. Reading ADC2 while Wi-Fi is active fails; this facade maps that to `ESP_ERR_E32P_RESOURCE_CONFLICT`. Prefer ADC1 for anything that runs with Wi-Fi. |
| Range | 12-bit (0..4095). 12 dB attenuation gives the widest usable span (~0..3.1 V); above that the reading saturates. |
| Calibration | Line-fitting; needs eFuse Vref/Two-Point data (present on most ESP32s). If absent, `read_voltage` returns `ESP_ERR_INVALID_STATE` — `read_raw` still works. |
| Input-only pins | ADC1 channels 6/7 are GPIO34/35 (input-only) — fine for ADC, not for output. |
