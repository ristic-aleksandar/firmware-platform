# e32p_spi — SPI master facade (v5.x driver)

Bus + device facade over the ESP-IDF v5.x `spi_master` driver. Init a host bus,
attach devices (each with its own CS / clock / mode), and do full-duplex or
one-way transfers.

## Enable

```
CONFIG_E32P_SPI_ENABLE=y
```
Then `REQUIRES e32p_spi`.

## API

| Function | Purpose |
|---|---|
| `e32p_spi_bus_init` / `_bus_deinit` | Init / free a host bus (SPI2 or SPI3). |
| `e32p_spi_add_device` / `_remove_device` | Attach / detach a device (CS/clock/mode). |
| `e32p_spi_transfer` | Full-duplex: send `tx` while receiving `rx` (either may be NULL). |
| `e32p_spi_write` / `e32p_spi_read` | One-way helpers over transfer. |

## Example

```c
e32p_spi_bus_config_t bcfg = { .host = 2, .sclk_pin = 18, .mosi_pin = 23,
                               .miso_pin = 19, .max_transfer_sz = 0 };
e32p_spi_bus_handle_t bus;
ESP_ERROR_CHECK(e32p_spi_bus_init(&bcfg, &bus));

e32p_spi_dev_config_t dcfg = { .cs_pin = 21, .clock_hz = 1000000, .mode = 0, .queue_size = 3 };
e32p_spi_dev_handle_t dev;
ESP_ERROR_CHECK(e32p_spi_add_device(bus, &dcfg, &dev));

uint8_t tx[2] = { 0x9F, 0x00 }, rx[2] = { 0 };
ESP_ERROR_CHECK(e32p_spi_transfer(dev, tx, rx, sizeof(tx)));   /* read a JEDEC ID, etc. */
```

## Notes / gotchas (ESP32)

| Item | Detail |
|---|---|
| Hosts | Use `host = 2` (SPI2/HSPI) or `3` (SPI3/VSPI). SPI1 is the flash bus — off limits. |
| No internal loopback | Unlike UART, SPI has no self-loopback. To self-test, jumper MOSI→MISO. |
| DMA buffers | Large transfers use DMA — buffers should be in internal RAM and word-aligned. Small stack buffers are fine. |
| Default pins used in examples | SCLK=18, MOSI=23, MISO=19, CS=21 — all non-strapping. Avoid GPIO12/15 (HSPI defaults, strapping). |
| Mode | `mode` is 0..3 (CPOL/CPHA); match the peripheral's datasheet. |
