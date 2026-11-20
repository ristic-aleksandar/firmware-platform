# e32p_i2s — I2S standard-mode audio facade

Thin facade over the ESP-IDF v5.x I2S standard (Philips) mode. One handle = one
channel: TX for playback (to an I2S DAC/amp) or RX for capture (from an I2S mic).

## Enable

```
CONFIG_E32P_I2S_ENABLE=y
```
Then `REQUIRES e32p_i2s`.

## API

| Function | Purpose |
|---|---|
| `e32p_i2s_init` / `_deinit` | Create + enable / release a channel. |
| `e32p_i2s_write` | Write PCM samples (TX). |
| `e32p_i2s_read` | Read PCM samples (RX). |

## Example (play a buffer)

```c
e32p_i2s_config_t cfg = { .port = 0, .dir = E32P_I2S_TX, .bclk_gpio = 26,
                          .ws_gpio = 25, .data_gpio = 22, .sample_rate = 16000,
                          .bits = 16, .stereo = false };
e32p_i2s_handle_t i2s;
ESP_ERROR_CHECK(e32p_i2s_init(&cfg, &i2s));

size_t written;
e32p_i2s_write(i2s, pcm, pcm_len, &written, 1000);
```

## Wiring

**Playback — MAX98357A / PCM5102 (I2S DAC/amp):**
| DAC pin | ESP32 |
|---|---|
| BCLK / SCK | GPIO26 |
| LRC / WS | GPIO25 |
| DIN | GPIO22 (DOUT) |
| VIN / GND | 3V3 / GND |

**Capture — INMP441 (I2S mic):** wire SCK→BCLK, WS→WS, SD→DIN (data_gpio), and use
`E32P_I2S_RX`.

## Notes / gotchas

| Item | Detail |
|---|---|
| Master clock | This facade configures the ESP32 as I2S **master** — it generates BCLK/WS, so TX drains at the sample rate even with no DAC attached (writes succeed). |
| Bits | 16 or 32 bits per sample. |
| Mono/stereo | Mono duplicates to both slots on TX; stereo interleaves L/R. |
| Ports | Port 0 or 1 (`SOC_I2S_NUM`). |
