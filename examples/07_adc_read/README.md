# 07_adc_read

Reads an ADC1 channel (GPIO34) and logs raw count + calibrated millivolts. Proves
`e32p_adc`. Self-contained on a bare board; a jumper makes it interactive.

## Build & flash

```powershell
. C:\Espressif\Initialize-Idf.ps1 -IdfId esp-idf-ab7213b7273352b64422b1f400ff27a0
# From the repository root
idf.py -C examples\07_adc_read set-target esp32
idf.py -C examples\07_adc_read -p COM5 build flash monitor
```

Expected:

```
I (…) adc_read: reading ADC1 ch6 (GPIO34) - jumper pin to 3V3 or GND to see it swing
I (…) adc_read: raw=1873  mV=1509      (floating: noisy, mid-ish)
```

## Make it move

- Jumper **GPIO34 → 3V3**: `raw` climbs toward ~4095, `mV` toward ~3100.
- Jumper **GPIO34 → GND**: `raw` ≈ 0, `mV` ≈ 0.
- A potentiometer wiper on GPIO34 (ends to 3V3/GND) sweeps the full range.

GPIO34 is input-only — fine for ADC. Use ADC1 (not ADC2) so this keeps working
once Wi-Fi is added.
