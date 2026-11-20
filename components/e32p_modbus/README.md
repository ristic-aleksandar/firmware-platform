# e32p_modbus — Modbus RTU master

A Modbus RTU **master** over [`e32p_uart`](../e32p_uart), plus a pure protocol
core and a pure slave request handler. Read and write registers on PLCs, energy
meters, VFDs, and industrial sensors over a serial (RS-485 / RS-232 / TTL) line.

## What it does

- **Master** function codes: `0x03` read holding, `0x04` read input, `0x06` write
  single register, `0x10` write multiple registers.
- **Exceptions:** a slave error reply surfaces as `ESP_ERR_INVALID_RESPONSE`, with
  the code available via `e32p_modbus_last_exception()`.
- **Pure protocol core:** CRC-16/Modbus, ADU framing (build + verify), request
  builders, and a read-response parser.
- **Pure slave handler:** `e32p_modbus_slave_process()` answers a request against a
  register bank — enough to build an in-firmware slave and to test the whole
  exchange headless.

## Design

Three layers, the first two with **no UART** so they are fully unit-tested:

1. **Protocol** — `crc16`, `adu_build`/`adu_check`, `build_read`/`build_write_*`,
   `parse_read_response`. Verified against the CRC catalogue (`"123456789"` →
   `0x4B37`) and textbook frames (`01 03 00 00 00 0A C5 CD`).
2. **Slave** — `slave_process` turns a request + register model into a response
   (or a Modbus exception). A test drives a **full master→slave→master round-trip**
   in memory.
3. **Master** — thin `transact` over `e32p_uart` (flush, write, read the reply by
   its deterministic length), then parse. This is the only layer needing a wire.

## API

```c
/* protocol core (pure) */
uint16_t  e32p_modbus_crc16(const uint8_t *data, size_t len);
int       e32p_modbus_adu_build(uint8_t *dst, size_t cap, uint8_t slave, uint8_t func,
                                const uint8_t *payload, size_t plen);
bool      e32p_modbus_adu_check(const uint8_t *adu, size_t len);
int       e32p_modbus_build_read(uint8_t *dst, size_t cap, uint8_t slave, uint8_t func,
                                 uint16_t addr, uint16_t count);
int       e32p_modbus_build_write_single(uint8_t *dst, size_t cap, uint8_t slave, uint16_t addr, uint16_t value);
int       e32p_modbus_build_write_multiple(uint8_t *dst, size_t cap, uint8_t slave, uint16_t addr,
                                           uint16_t count, const uint16_t *values);
esp_err_t e32p_modbus_parse_read_response(const uint8_t *adu, size_t len, uint8_t slave, uint8_t func,
                                          uint16_t *out, size_t max, size_t *out_count, uint8_t *out_exc);

/* slave handler (pure) */
int e32p_modbus_slave_process(e32p_modbus_slave_t *slave, const uint8_t *req, size_t req_len,
                              uint8_t *resp, size_t cap);

/* master (UART) */
esp_err_t e32p_modbus_master_init(const e32p_modbus_master_config_t *cfg, e32p_modbus_master_handle_t *out);
esp_err_t e32p_modbus_master_deinit(e32p_modbus_master_handle_t h);
esp_err_t e32p_modbus_read_holding_registers(e32p_modbus_master_handle_t h, uint8_t slave, uint16_t addr, uint16_t count, uint16_t *out);
esp_err_t e32p_modbus_read_input_registers (e32p_modbus_master_handle_t h, uint8_t slave, uint16_t addr, uint16_t count, uint16_t *out);
esp_err_t e32p_modbus_write_single_register(e32p_modbus_master_handle_t h, uint8_t slave, uint16_t addr, uint16_t value);
esp_err_t e32p_modbus_write_multiple_registers(e32p_modbus_master_handle_t h, uint8_t slave, uint16_t addr, uint16_t count, const uint16_t *values);
uint8_t   e32p_modbus_last_exception(e32p_modbus_master_handle_t h);
```

## Usage

```c
e32p_modbus_master_config_t cfg = E32P_MODBUS_MASTER_CONFIG_DEFAULT();  /* uart1, 9600 8E1 */
e32p_modbus_master_handle_t mb;
ESP_ERROR_CHECK(e32p_modbus_master_init(&cfg, &mb));

uint16_t regs[4];
esp_err_t err = e32p_modbus_read_holding_registers(mb, /*slave*/ 1, /*addr*/ 0, /*count*/ 4, regs);
if (err == ESP_OK) {
    printf("reg0=%u reg1=%u\n", regs[0], regs[1]);
} else if (err == ESP_ERR_INVALID_RESPONSE) {
    printf("slave exception 0x%02x\n", e32p_modbus_last_exception(mb));
}

e32p_modbus_write_single_register(mb, 1, 0x0010, 1234);
```

## Wiring / RS-485 note

The master speaks over a plain UART, so it works directly with **TTL** or **RS-232**
(via a level shifter) slaves. For **RS-485** you also need a transceiver
(e.g. MAX485). Half-duplex RS-485 needs the driver-enable (DE/RE) line switched
around each transmit — use an **auto-direction** transceiver module, or drive DE
from the UART's RTS in hardware RS-485 mode. (Automatic DE/RE from this module is
a planned addition; today it targets TTL/RS-232 and auto-direction RS-485.)

Modbus RTU defaults to **8E1** (even parity). Many devices use 8N1 — match the
slave via the `parity` / `stop_bits` config fields.

## Cherry-pick

```
CONFIG_E32P_MODBUS_ENABLE=y   # selects E32P_UART_ENABLE
```

Requires `e32p_core` and `e32p_uart`.
