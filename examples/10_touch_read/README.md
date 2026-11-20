# 10_touch_read

Reads capacitive touch pad **T0 (GPIO4)** and flags a touch. Proves `e32p_touch`.
Fully interactive with just a finger — no external parts.

## Build & flash

```powershell
. C:\Espressif\Initialize-Idf.ps1 -IdfId esp-idf-ab7213b7273352b64422b1f400ff27a0
idf.py -C C:\Users\aleks\Work\esp32_platform\examples\10_touch_read set-target esp32
idf.py -C C:\Users\aleks\Work\esp32_platform\examples\10_touch_read -p COM5 build flash monitor
```

Expected — a steady resting value, dropping (and showing `[TOUCHED]`) when you
touch or hover over the GPIO4 pin:

```
I (…) touch: T0 (GPIO4) baseline=680 threshold=544 - touch the GPIO4 pin to trigger
I (…) touch: touch= 679
I (…) touch: touch= 681
I (…) touch: touch= 210 [TOUCHED]     <- finger on GPIO4
I (…) touch: touch= 190 [TOUCHED]
I (…) touch: touch= 678
```

Touch the **GPIO4** pin on the DevKit header (or press a jumper wire into it and
touch the wire for a bigger swing). The value falls because your finger adds
capacitance. Exact numbers vary with board and humidity — that's why the demo
uses a startup baseline rather than a fixed level.
