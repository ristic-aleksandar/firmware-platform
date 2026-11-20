# e32p_i2c — I2C master facade (v5.x driver)

Bus + device facade over the ESP-IDF v5.x `i2c_master` driver. Create a bus,
probe/scan for devices, attach devices, and do byte or register transfers.

## Enable

```
CONFIG_E32P_I2C_ENABLE=y
```
Then `REQUIRES e32p_i2c`.

## API

| Function | Purpose |
|---|---|
| `e32p_i2c_bus_init` / `_bus_deinit` | Create / delete a master bus. |
| `e32p_i2c_probe` | Check if one address ACKs. |
| `e32p_i2c_scan` | Sweep 0x08..0x77, collect present addresses. |
| `e32p_i2c_add_device` / `_remove_device` | Attach / detach a device (own SCL speed). |
| `e32p_i2c_write` / `_read` / `_write_read` | Byte transfers. |
| `e32p_i2c_read_reg` / `_write_reg` | Register access (the common sensor pattern). |

## Example (read a sensor register)

```c
e32p_i2c_bus_config_t bcfg = { .port = 0, .sda_pin = 21, .scl_pin = 22, .enable_pullups = true };
e32p_i2c_bus_handle_t bus;
ESP_ERROR_CHECK(e32p_i2c_bus_init(&bcfg, &bus));

e32p_i2c_dev_handle_t dev;
ESP_ERROR_CHECK(e32p_i2c_add_device(bus, 0x76, 400000, &dev));   /* BMP280 @ 400 kHz */

uint8_t id = 0;
ESP_ERROR_CHECK(e32p_i2c_read_reg(dev, 0xD0, &id, 1, 100));      /* chip-id register */
```

## Notes / gotchas

| Item | Detail |
|---|---|
| Driver | Uses the **new** `i2c_master` driver. The legacy `driver/i2c.h` is deprecated and not used here. |
| Pull-ups | Internal pull-ups are weak (~45 kΩ). Use external 2.2–4.7 kΩ resistors for anything real, especially at 400 kHz. |
| Addresses | 7-bit throughout. `scan` skips the reserved ranges (below 0x08, above 0x77). |
| Per-device speed | Each attached device carries its own `scl_speed_hz`; the bus runs at the active device's clock. |
| Pins | SDA and SCL must be output-capable (they are open-drain, bidirectional). |
