# 09_dac_adc_loopback

Drives GPIO25 with the DAC and reads it back with **ADC1 on GPIO34**. Proves
`e32p_dac` and `e32p_adc` together. Needs **one jumper wire: GPIO25 → GPIO34**.

## Why a jumper (and not GPIO25's own ADC2)

GPIO25 is DAC channel 0 *and* ADC2 channel 8, so an on-chip readback seems
possible — but the DAC output buffer and the ADC2 input contend for the same RTC
pad through the oneshot drivers, and the ADC just reads a floating floor (~200 mV,
no correlation). Reading the DAC back on an **independent** unit (ADC1, which
does not share that pad) works reliably — hence the single jumper.

## Build & flash

```powershell
. C:\Espressif\Initialize-Idf.ps1 -IdfId esp-idf-ab7213b7273352b64422b1f400ff27a0
# From the repository root
idf.py -C examples\09_dac_adc_loopback set-target esp32
idf.py -C examples\09_dac_adc_loopback -p COM5 build flash monitor
```

Without the jumper, GPIO34 floats — the ADC reads near 0 and does not track. With
**GPIO25 → GPIO34** wired:

```
I (…) dac_adc: dac=  0 (~   0 mV) -> adc raw=  10 mv= 140
I (…) dac_adc: dac= 64 (~ 828 mV) -> adc raw=1010 mv= 820
I (…) dac_adc: dac=128 (~1656 mV) -> adc raw=2020 mv=1630
I (…) dac_adc: dac=192 (~2484 mV) -> adc raw=3050 mv=2470
I (…) dac_adc: dac=255 (~3300 mV) -> adc raw=4095 mv=3150
```

The ADC value climbs in lockstep with the DAC code — analog out and in proven.
No jumper handy? You can also measure GPIO25 directly with a multimeter and watch
it step 0 → ~3.3 V.
