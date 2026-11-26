# 15_i2s_tone

Streams a 440 Hz sine tone over I2S TX. Proves `e32p_i2s`. On a bare board it
proves the TX/clock path; wire a MAX98357A amp and you'll hear the tone.

## Build & flash

```powershell
. C:\Espressif\Initialize-Idf.ps1 -IdfId esp-idf-ab7213b7273352b64422b1f400ff27a0
# From the repository root
idf.py -C examples\15_i2s_tone set-target esp32
idf.py -C examples\15_i2s_tone -p COM5 build flash monitor
```

Expected (bare board):

```
I (…) e32p_i2s: i2s0 TX init: bclk=26 ws=25 data=22 16000Hz 16bit mono
I (…) i2s_tone: streaming 440 Hz on DOUT=22 - wire a MAX98357A (DIN<-22 BCLK<-26 LRC<-25) to hear it
I (…) i2s_tone: streaming 440 Hz tone, 31 KB/s
I (…) i2s_tone: streaming 440 Hz tone, 31 KB/s
```

Steady ~31 KB/s (16000 samples/s × 2 bytes) with no errors proves the I2S engine
is clocking data out.

## Hear it — MAX98357A

| MAX98357A | ESP32 |
|---|---|
| BCLK | GPIO26 |
| LRC | GPIO25 |
| DIN | GPIO22 |
| VIN | 3V3 |
| GND | GND |
| (speaker across `+` / `-`) | — |

Power up with the amp wired and a small speaker attached — you'll hear a clean
440 Hz tone (concert A).
