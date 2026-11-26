# 08_gptimer_alarm

A 1 Hz hardware-timer alarm counted over serial. Proves `e32p_gptimer`: a periodic
alarm ISR increments a counter; main logs the count and the raw microsecond tick.
No external parts.

## Build & flash

```powershell
. C:\Espressif\Initialize-Idf.ps1 -IdfId esp-idf-ab7213b7273352b64422b1f400ff27a0
# From the repository root
idf.py -C examples\08_gptimer_alarm set-target esp32
idf.py -C examples\08_gptimer_alarm -p COM5 build flash monitor
```

Expected — `alarms` ticks up once per second. Because `auto_reload` re-arms the
timer, the raw microsecond count **resets to ~0 at each alarm**, so the 500 ms
samples alternate between ~4 000 and ~504 000 µs:

```
I (…) gptimer: 1 Hz hardware alarm running
I (…) gptimer: alarms=0  raw=4935 us
I (…) gptimer: alarms=0  raw=504230 us
I (…) gptimer: alarms=1  raw=4230 us      <- alarm fired: count++, timer reloaded
I (…) gptimer: alarms=1  raw=504230 us
I (…) gptimer: alarms=2  raw=4230 us
```

The alarm count increments exactly once per ~1 s — that's the hardware timer
firing its ISR on schedule. (Set `auto_reload = false` for a one-shot instead.)
