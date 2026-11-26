# 34_dsp_fixed_point

A FIR filter ported from Python (float) to C (Q15 fixed point), and checked on the chip.

It is a worked example of the job "port my DSP algorithm to a microcontroller". The goal is to prove the C
code gives the same answer as the original, and to measure what it costs on the target.

## What it does

1. `tools/gen_vectors.py` is the reference. It designs a 31-tap low-pass filter, makes a test signal
   (a wanted tone, an unwanted tone and a little noise), and filters it in float with numpy. It also
   runs a model of the integer code, bit for bit.
2. `main/dsp_fir.c` is the port: a Q15 FIR filter with a 32-bit accumulator, rounding and saturation.
3. `main/main.c` runs it on the board and checks two things:
   - The output equals the numpy model exactly (0 mismatches).
   - The output is within 3 LSB of the float reference.

   Then it times the Q15 and the float versions on the same data.

Run `python tools/gen_vectors.py` to rebuild `main/dsp_vectors.h`. The build itself does not need Python.

## Result on the bench (ESP32-WROOM-32, 240 MHz, ESP-IDF v5.3.1, `-O2`)

| Check | Result |
|---|---|
| Output vs numpy integer model | 0 mismatches in 1024 samples |
| Output vs float reference | max 1 LSB, mean 0.26 LSB |
| Q15 version, one 1024-sample block | 890 us (about 6.7 cycles per tap and sample) |
| Float32 version, same block | 750 us (about 5.6 cycles per tap and sample) |

Fixed point is not faster here. The classic ESP32 has a hardware floating point unit, so float32 keeps
up with Q15. Fixed point still saves memory (2 bytes per sample, not 4), and it is the right choice on chips
with no FPU. Measure on your own chip before you pick.

The first run was 4 times slower for Q15. It read its input and coefficients straight from flash, while the
float version read from RAM. Copying the data to RAM first made the comparison fair. A real filter reads
from an ADC or I2S buffer in RAM anyway.

## Limits

- The 32-bit accumulator is safe only when the sum of the absolute coefficients is below 65536. The code
  checks this and refuses to run otherwise.
- One block, zero start state. It does not keep filter state across blocks.
- Not tested on the ESP32-S3 or on a chip without an FPU.

## Build and flash

```powershell
idf.py -C examples/34_dsp_fixed_point -p COM5 build flash monitor
```

Look for `RESULT PASS`. It repeats every 3 seconds.
