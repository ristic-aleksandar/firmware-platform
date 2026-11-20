# e32p_ws2812 — WS2812 / NeoPixel driver over RMT

Addressable-LED driver for WS2812/WS2812B/NeoPixel strips, built on the ESP-IDF
v5.x RMT TX driver. You stage colors in a per-pixel buffer, then `refresh()`
clocks the whole strip out in one RMT transaction.

## Why RMT

WS2812 encodes each bit as a precisely-timed pulse (~1.25 µs/bit). The RMT
peripheral generates those pulses in hardware from a compact symbol stream, so
the CPU is free during transmission. This module wires up a **composite encoder**
— a bytes-encoder for the GRB data plus a copy-encoder for the ≥50 µs reset/latch
pulse — matching the ESP-IDF `rmt/led_strip` reference design.

## API

```c
esp_err_t e32p_ws2812_init(int gpio, int num_leds, e32p_ws2812_handle_t *out);
esp_err_t e32p_ws2812_deinit(e32p_ws2812_handle_t h);
esp_err_t e32p_ws2812_set_pixel(e32p_ws2812_handle_t h, int index,
                                uint8_t r, uint8_t g, uint8_t b);
esp_err_t e32p_ws2812_clear(e32p_ws2812_handle_t h);
esp_err_t e32p_ws2812_refresh(e32p_ws2812_handle_t h);
```

`set_pixel` / `clear` only touch the RAM buffer. The strip changes only on
`refresh`, which also waits for the latch — so a frame is atomic.

## Usage

```c
e32p_ws2812_handle_t strip;
ESP_ERROR_CHECK(e32p_ws2812_init(18, 8, &strip));   /* 8 LEDs on GPIO18 */

e32p_ws2812_clear(strip);
e32p_ws2812_set_pixel(strip, 0, 32, 0, 0);          /* dim red on pixel 0 */
e32p_ws2812_refresh(strip);                          /* clock it out */
```

## Wiring

| Strip pin | ESP32 |
|-----------|-------|
| DIN       | data GPIO (e.g. GPIO18) |
| +5V       | 5V (VIN/USB 5V) — the ESP32's 3V3 rail cannot power many LEDs |
| GND       | GND (common with the ESP32) |

Notes:
- The 3.3 V data line usually drives a WS2812 fine at short lengths; a level
  shifter to 5 V is the robust choice for long runs.
- Give the strip its own 5 V supply for more than a handful of bright LEDs;
  share grounds.

## Notes

| Topic | Detail |
|-------|--------|
| Color order | WS2812 is **GRB** on the wire; the API takes normal `r,g,b` and reorders internally. |
| Resolution | 10 MHz RMT (0.1 µs/tick). Bit0 = 0.3 µs high / 0.9 µs low, bit1 = 0.9 µs high / 0.3 µs low. |
| Reset/latch | ≥50 µs low after the last pixel, emitted by the copy-encoder. |
| Bare board | `init`/`refresh` succeed with nothing attached — the RMT engine still clocks the frame out. Use to prove the path before wiring LEDs. |
| Target support | `depends on SOC_RMT_SUPPORTED`. |

## Cherry-pick

```
CONFIG_E32P_WS2812_ENABLE=y
```

Requires `e32p_core`; privately requires `esp_driver_rmt`.
