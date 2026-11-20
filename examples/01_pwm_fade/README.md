# 01_pwm_fade

Breathes an LED on **GPIO18** using `e32p_ledc`. Proves the M1 slice: the
opaque-handle lifecycle (`init` → `start`), hardware fade, and status read.

## What it demonstrates

- The stateful module pattern: `e32p_ledc_init` (configures, leaves idle) →
  `e32p_ledc_start` → `e32p_ledc_fade`.
- Cherry-pick: only `CONFIG_E32P_LEDC_ENABLE=y`, so just `e32p_core` +
  `e32p_ledc` compile in.

## Build & flash

```powershell
. C:\Espressif\Initialize-Idf.ps1 -IdfId esp-idf-ab7213b7273352b64422b1f400ff27a0
idf.py -C C:\Users\aleks\Work\esp32_platform\examples\01_pwm_fade set-target esp32
idf.py -C C:\Users\aleks\Work\esp32_platform\examples\01_pwm_fade -p COM5 build flash monitor
```

Expected serial output (repeats):

```
I (…) pwm_fade: fading on GPIO18, max_duty=8191
I (…) pwm_fade: fade -> HIGH over 1000 ms
I (…) pwm_fade: fade -> LOW over 1000 ms
```

## Wiring note

With an LED (+ ~330 Ω resistor) from GPIO18 to GND, it smoothly breathes up and
down. On a bare board the fade log alternates every ~1.2 s — the PWM is running
regardless. Change `PWM_PIN` in `main/main.c` for a different output pin.
