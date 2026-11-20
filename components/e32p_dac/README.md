# e32p_dac — 8-bit DAC facade

Oneshot 8-bit DAC output over the ESP-IDF v5.x `dac_oneshot` driver. ESP32 has
two DAC channels: **ch0 = GPIO25**, **ch1 = GPIO26**.

## Enable

```
CONFIG_E32P_DAC_ENABLE=y
```
Then `REQUIRES e32p_dac`. (Hidden on SoCs without a DAC — S3/C3/C6.)

## API

| Function | Purpose |
|---|---|
| `e32p_dac_init` / `_deinit` | Open / release a channel (0 or 1). |
| `e32p_dac_set_raw` | Output an 8-bit code (0..255). |
| `e32p_dac_set_voltage` | Output ~millivolts (0..3300, clamped). |

## Example

```c
e32p_dac_handle_t dac;
ESP_ERROR_CHECK(e32p_dac_init(0, &dac));        /* GPIO25 */
ESP_ERROR_CHECK(e32p_dac_set_voltage(dac, 1650)); /* ~half scale */
```

## Notes / gotchas

| Item | Detail |
|---|---|
| Pins | ch0 is fixed to GPIO25, ch1 to GPIO26 — not remappable. |
| Resolution | 8-bit (256 steps). Output is ratiometric to Vdd (~3.3 V), so `set_voltage` is approximate. |
| Self-test | Read the DAC back with `e32p_adc` on an **independent** unit (ADC1, e.g. GPIO34) via a jumper — see `examples/09_dac_adc_loopback`. GPIO25's own ADC2 (ch8) shares the DAC's RTC pad and does **not** give a working internal readback through the oneshot drivers. |
| Availability | ESP32 and ESP32-S2 only. |
