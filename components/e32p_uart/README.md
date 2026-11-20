# e32p_uart — UART port facade

Opaque-handle wrapper over the ESP-IDF UART driver. One handle per port, with
blocking write, timed read, RX buffering, and an internal loopback for self-test.

## Enable

```
CONFIG_E32P_UART_ENABLE=y
```
Then `REQUIRES e32p_uart`.

## API

| Function | Purpose |
|---|---|
| `e32p_uart_init` / `e32p_uart_deinit` | Install / uninstall a port. |
| `e32p_uart_write` | Blocking write. Returns bytes written, or -1. |
| `e32p_uart_read` | Timed read. Returns bytes read (may be < len), or -1. |
| `e32p_uart_flush_input` | Discard buffered RX. |
| `e32p_uart_available` | Bytes buffered in the RX ring. |
| `e32p_uart_set_loopback` | Internal TX->RX (test without wiring). |

Note: `write`/`read` return **byte counts** (POSIX-style), not `esp_err_t` — the
natural shape for stream I/O.

## Example

```c
e32p_uart_config_t cfg = E32P_UART_CONFIG_DEFAULT();
cfg.port = 1; cfg.tx_pin = 17; cfg.rx_pin = 16; cfg.baud_rate = 115200;

e32p_uart_handle_t uart;
ESP_ERROR_CHECK(e32p_uart_init(&cfg, &uart));

e32p_uart_write(uart, "hello\n", 6);
char buf[32];
int n = e32p_uart_read(uart, buf, sizeof(buf), 100);   /* wait up to 100 ms */
```

## Notes / gotchas (ESP32)

| Item | Detail |
|---|---|
| Ports | UART0 is the console — this facade rejects port 0. Use 1 or 2. |
| TX buffer | Installed with a 0-byte TX buffer, so `write` blocks until the bytes are queued. |
| Loopback | `e32p_uart_set_loopback(h, true)` routes TX to RX on-chip — great for tests and bring-up without a second device. |
| Pins | TX must be output-capable; RX any valid GPIO. Input-only pins (34–39) work for RX, not TX. |
