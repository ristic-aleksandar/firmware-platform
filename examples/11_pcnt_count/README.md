# 11_pcnt_count

Counts internally-generated pulses with the PCNT peripheral — **no wiring**.
Proves `e32p_pcnt` + `e32p_gpio`. `io_loop_back` feeds GPIO18's own output back to
the PCNT input, so toggling the pin generates countable edges.

## Build & flash

```powershell
. C:\Espressif\Initialize-Idf.ps1 -IdfId esp-idf-ab7213b7273352b64422b1f400ff27a0
idf.py -C C:\Users\aleks\Work\esp32_platform\examples\11_pcnt_count set-target esp32
idf.py -C C:\Users\aleks\Work\esp32_platform\examples\11_pcnt_count -p COM5 build flash monitor
```

Expected — the count exactly equals the pulses generated each cycle:

```
I (…) pcnt: counting rising edges on GPIO18 (internally generated, no wiring)
I (…) pcnt: generated 50 pulses -> pcnt count = 50  [PASS]
I (…) pcnt: generated 50 pulses -> pcnt count = 50  [PASS]
```

To count a real signal instead, set `io_loop_back = false` and wire your source
(a sensor, encoder, or square wave) to GPIO18.
