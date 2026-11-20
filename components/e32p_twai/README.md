# e32p_twai — CAN 2.0 (TWAI) facade

Thin facade over the ESP-IDF TWAI (CAN 2.0) controller. The ESP32 has one TWAI
peripheral, so the API is a singleton (no handle).

## Enable

```
CONFIG_E32P_TWAI_ENABLE=y
```
Then `REQUIRES e32p_twai`. (Hidden on SoCs without TWAI.)

## API

| Function | Purpose |
|---|---|
| `e32p_twai_init` / `_deinit` | Install / uninstall the driver. |
| `e32p_twai_start` / `_stop` | Enter / leave the running state. |
| `e32p_twai_transmit` | Queue a frame (up to 8 data bytes). |
| `e32p_twai_receive` | Receive a frame (with timeout). |

## Example (self-test loopback)

```c
e32p_twai_config_t cfg = { .tx_gpio = 21, .rx_gpio = 22, .bitrate_bps = 500000,
                           .mode = E32P_TWAI_MODE_NO_ACK };
ESP_ERROR_CHECK(e32p_twai_init(&cfg));
ESP_ERROR_CHECK(e32p_twai_start());

e32p_twai_message_t tx = { .id = 0x123, .self = true, .len = 2, .data = { 0xAB, 0xCD } };
e32p_twai_transmit(&tx, 100);
e32p_twai_message_t rx;
e32p_twai_receive(&rx, 100);       /* needs TX<->RX wire or a transceiver */
```

## Notes / gotchas

| Item | Detail |
|---|---|
| **Transceiver** | A real CAN bus needs an external transceiver (SN65HVD230 / TJA1050). The ESP32 has only the controller. |
| **Bring-up glitch (IDF 5.3.1)** | `twai_driver_install()` makes the TX pin a plain GPIO output and prints a log line before it hands the pin to the controller. In that gap the pin drives its output register, which is 0 (dominant) after reset: ~8 ms of dominant bus at every install. `e32p_twai_init()` presets that bit to 1, so the gap stays recessive. |
| Wire-only test | With no transceiver, jumper **TX to RX** and use `E32P_TWAI_MODE_NO_ACK` + `self=true` to loop a frame back. NO_ACK does **not** self-loop internally. |
| Bitrates | 125 k / 250 k / 500 k / 1 M bps (fixed presets). Others return `ESP_ERR_INVALID_ARG`. |
| Modes | `NORMAL` (real bus), `NO_ACK` (self-test), `LISTEN_ONLY` (sniff without ACKing). |
| Singleton | One controller — `init` twice returns `ESP_ERR_INVALID_STATE`. |
| **Queue depths** | `CONFIG_E32P_TWAI_RX_QUEUE_LEN` / `CONFIG_E32P_TWAI_TX_QUEUE_LEN`, both default **5** (the IDF default — existing users are unaffected). Range 5–64. |
| Why raise RX | 5 frames is ~2.5 ms of wire time at 250 kbit but only ~0.6 ms at 1 Mbit. A higher-priority task preempting mid-burst overruns it. |
| Why raise TX | TX depth bounds burst throughput: a sender refilling once per poll tick moves at most that many frames per tick, so a shallow queue caps a segmented transport (ISO-TP) regardless of bitrate. |
| **Deep TX + stop** | `e32p_twai_transmit` only *queues*. With a deep TX queue, more frames are still in flight when you stop — poll `twai_get_status_info().msgs_to_tx` to 0 before `e32p_twai_stop()`, or the tail of a transfer is aborted and the peer never sees it. |
