# e32p_bmp280 — Bosch BMP280 pressure & temperature

A driver for the Bosch BMP280 over [`e32p_i2c`](../e32p_i2c). Reads the factory
calibration, configures oversampling/filter/mode, and returns compensated
temperature and absolute pressure.

## What it does

- **Probes** the chip id (0x58) and fails cleanly if the wrong/absent part answers.
- **Reads** the 24-byte factory calibration block (regs 0x88–0x9F).
- **Configures** oversampling (temp/press), IIR filter, standby, and power mode.
- **Samples** with a single 6-byte burst read and applies the Bosch compensation
  math, returning temperature in 0.01 °C and pressure in pascals.

## Design

Split in two so the hard part is testable without hardware:

- **Pure** — `parse_calib`, `compensate_temp`, `compensate_press` are plain
  functions of their inputs (the datasheet's fixed-point algorithm). The unit
  test drives them with the Bosch reference vector and asserts the published
  result (`t_fine=128422`, `25.08 °C`, `100653 Pa`).
- **Thin I2C** — `init` / `read` wrap `e32p_i2c` register transfers. Opaque
  handle, one per sensor; the I2C bus is shared and owned by the caller.

## API

```c
/* pure */
void     e32p_bmp280_parse_calib(const uint8_t raw[24], e32p_bmp280_calib_t *out);
int32_t  e32p_bmp280_compensate_temp(int32_t adc_t, const e32p_bmp280_calib_t *c, int32_t *t_fine);
uint32_t e32p_bmp280_compensate_press(int32_t adc_p, const e32p_bmp280_calib_t *c, int32_t t_fine);

/* device */
esp_err_t e32p_bmp280_init(const e32p_bmp280_config_t *cfg, e32p_bmp280_handle_t *out);
esp_err_t e32p_bmp280_deinit(e32p_bmp280_handle_t h);
esp_err_t e32p_bmp280_read(e32p_bmp280_handle_t h, e32p_bmp280_reading_t *out);
esp_err_t e32p_bmp280_read_raw(e32p_bmp280_handle_t h, int32_t *adc_t, int32_t *adc_p);
const e32p_bmp280_calib_t *e32p_bmp280_calib(e32p_bmp280_handle_t h);
```

## Usage

```c
e32p_i2c_bus_config_t bc = { .port = 0, .sda_pin = 21, .scl_pin = 22, .enable_pullups = true };
e32p_i2c_bus_handle_t bus;
e32p_i2c_bus_init(&bc, &bus);

e32p_bmp280_config_t cfg = E32P_BMP280_CONFIG_DEFAULT(bus);   /* 0x76, normal mode */
e32p_bmp280_handle_t sensor;
ESP_ERROR_CHECK(e32p_bmp280_init(&cfg, &sensor));

e32p_bmp280_reading_t r;
if (e32p_bmp280_read(sensor, &r) == ESP_OK) {
    printf("%.2f C  %lu Pa\n", r.temperature_c100 / 100.0, (unsigned long)r.pressure_pa);
}
```

## Wiring (BMP280 module, 3.3 V)

| Sensor | ESP32 | Note |
|--------|-------|------|
| VCC | 3V3 | Module has its own 3.3 V regulator/level shifting on most boards. |
| GND | GND | |
| SCL | GPIO22 | Bus default; external pull-ups usually on the module. |
| SDA | GPIO21 | |
| SDO | GND / 3V3 | Selects address: low = 0x76, high = 0x77. |
| CSB | 3V3 | Tie high to select I2C (not SPI). |

## Register map (used)

| Reg | Name | Purpose |
|-----|------|---------|
| 0xD0 | id | 0x58 for BMP280 |
| 0xE0 | reset | write 0xB6 to soft-reset |
| 0x88–0x9F | calib | 12 little-endian coefficients (T1–T3, P1–P9) |
| 0xF4 | ctrl_meas | osrs_t[7:5], osrs_p[4:2], mode[1:0] |
| 0xF5 | config | t_sb[7:5], filter[4:2], spi3w_en[0] |
| 0xF7–0xFC | data | press[19:0], temp[19:0] |

## Cherry-pick

```
CONFIG_E32P_BMP280_ENABLE=y   # selects E32P_I2C_ENABLE
```

Requires `e32p_core` and `e32p_i2c`.
