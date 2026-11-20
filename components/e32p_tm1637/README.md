# e32p_tm1637 — TM1637 4-digit 7-segment display

A driver for TM1637-based 4-digit displays (the common clock modules with a
colon). Formats numbers and drives the display over the TM1637's I2C-like 2-wire
protocol, bit-banged on two GPIOs.

## What it does

- **Encodes** hex digits (0–F) to 7-segment bytes and **formats** signed
  integers (−999…9999) with optional leading zeros, colon, and a minus sign.
- **Drives** the display: start/stop framing, per-byte ACK, brightness control
  (0–7), raw-segment writes, and a clear.

> The TM1637 looks like I2C but has **no device address** and clocks data
> **LSB-first**, so it can't use the I2C peripheral — it's bit-banged over two
> open-drain lines via [`e32p_gpio`](../e32p_gpio).

## Design

- **Pure** — `encode_digit` and `format_int` are plain functions with no I/O, so
  the font and the digit layout (right-alignment, padding, colon, sign) are fully
  unit-tested headless.
- **Thin GPIO** — `init` / `set_segments` / `show_int` bit-bang the protocol.
  Opaque handle, one per display.

## API

```c
/* pure */
uint8_t   e32p_tm1637_encode_digit(uint8_t digit);
esp_err_t e32p_tm1637_format_int(int value, bool leading_zeros, bool colon, uint8_t out[4]);

/* device */
esp_err_t e32p_tm1637_init(const e32p_tm1637_config_t *cfg, e32p_tm1637_handle_t *out);
esp_err_t e32p_tm1637_deinit(e32p_tm1637_handle_t h);
esp_err_t e32p_tm1637_set_brightness(e32p_tm1637_handle_t h, uint8_t level);   /* 0..7 */
esp_err_t e32p_tm1637_set_segments(e32p_tm1637_handle_t h, const uint8_t *seg, size_t len, uint8_t pos);
esp_err_t e32p_tm1637_show_int(e32p_tm1637_handle_t h, int value, bool leading_zeros, bool colon);
esp_err_t e32p_tm1637_clear(e32p_tm1637_handle_t h);
```

## Usage

```c
e32p_tm1637_config_t cfg = { .clk_pin = 18, .dio_pin = 19, .brightness = 4 };
e32p_tm1637_handle_t disp;
ESP_ERROR_CHECK(e32p_tm1637_init(&cfg, &disp));

e32p_tm1637_show_int(disp, 1234, false, false);   /* "1234" */
e32p_tm1637_show_int(disp, 1205, true, true);      /* "12:05" as a clock */
e32p_tm1637_clear(disp);
```

## Wiring (TM1637 module)

| Module | ESP32 | Note |
|--------|-------|------|
| VCC | 3V3 (or 5V) | Module runs at 3.3–5 V; 3.3 V logic is fine. |
| GND | GND | |
| CLK | GPIO18 | Any output-capable GPIO. |
| DIO | GPIO19 | Any output-capable GPIO (bidirectional for ACK). |

The board carries its own pull-ups; the driver also enables the internal ones.
Avoid strapping pins (GPIO0/2/12/15) for CLK/DIO.

## Protocol notes

| Item | Value |
|------|-------|
| Framing | I2C-like start/stop, ACK after each byte |
| Bit order | LSB first |
| Data command | 0x40 (auto-increment address, write) |
| Address command | 0xC0 \| position (0–3) |
| Display control | 0x88 \| brightness (0–7) |
| Colon | bit7 of digit position 1 |

## Cherry-pick

```
CONFIG_E32P_TM1637_ENABLE=y   # selects E32P_GPIO_ENABLE
```

Requires `e32p_core`; privately requires `e32p_gpio` and `esp_rom` (delay).
