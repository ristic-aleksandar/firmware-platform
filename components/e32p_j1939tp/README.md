# e32p_j1939tp — J1939-21 Transport Protocol

Segments and reassembles **9–1785 byte** J1939 messages over
[`e32p_j1939`](../e32p_j1939), via **BAM** (broadcast) and **RTS/CTS**
(connection-mode), transmit and receive.

## Design: injected sink + tick

The engine is decoupled from both the bus and the clock, which makes it fully
headless-testable:

- **Outgoing frames** go through an injected `tx` callback. The application
  wires it to `e32p_j1939_send`; a test can capture the frames or feed them into
  a second engine for an in-process loopback.
- **Incoming frames**: feed every received `TP.CM` (PGN 60416) and `TP.DT`
  (PGN 60160) frame to `e32p_j1939tp_on_frame`.
- **Time** is supplied by `e32p_j1939tp_poll(now_ms)`, which services timers and
  paces outgoing packets.

This is the same main-function-tick shape as AUTOSAR's J1939Tp. A handle is
driven from **one task** (serialise `send`/`on_frame`/`poll` in a single loop);
it is not ISR- or multi-task-safe.

## API

```c
esp_err_t e32p_j1939tp_init(const e32p_j1939tp_config_t *cfg, e32p_j1939tp_handle_t *out);
esp_err_t e32p_j1939tp_deinit(e32p_j1939tp_handle_t h);
esp_err_t e32p_j1939tp_send(e32p_j1939tp_handle_t h, uint32_t pgn, uint8_t priority,
                            uint8_t dst, const uint8_t *data, uint16_t len);
esp_err_t e32p_j1939tp_on_frame(e32p_j1939tp_handle_t h, const e32p_j1939_message_t *frame);
void      e32p_j1939tp_poll(e32p_j1939tp_handle_t h, uint32_t now_ms);
bool      e32p_j1939tp_tx_busy(e32p_j1939tp_handle_t h);
```

`dst == 0xFF` (global) sends **BAM**; any other destination sends **RTS/CTS**.
`send` copies the payload; completion/failure is reported via `on_tx_done`, and
a fully reassembled inbound message via `on_rx`.

## Wiring it to the bus

```c
static esp_err_t tp_tx(const e32p_j1939_message_t *f, void *user) {
    return e32p_j1939_send(f, 50);          /* engine frame -> real CAN */
}
static void tp_rx(uint32_t pgn, uint8_t src, uint8_t dst,
                  const uint8_t *data, uint16_t len, void *user) {
    /* a complete multi-packet message arrived */
}

e32p_j1939tp_config_t cfg = { .src_addr = 0x80, .tx = tp_tx, .on_rx = tp_rx };
e32p_j1939tp_handle_t tp;
e32p_j1939tp_init(&cfg, &tp);

/* In the app's CAN task loop: */
e32p_j1939_message_t m;
if (e32p_j1939_receive(&m, 10) == ESP_OK) e32p_j1939tp_on_frame(tp, &m);
e32p_j1939tp_poll(tp, esp_timer_get_time() / 1000);
```

## Protocol details

| Item | Value |
|------|-------|
| TP.CM PGN | 60416 (0xEC00) — BAM 0x20, RTS 0x10, CTS 0x11, EndOfMsgAck 0x13, Abort 0xFF |
| TP.DT PGN | 60160 (0xEB00) — seq(1..255) + 7 data bytes |
| Max message | 1785 bytes (255 × 7) — ETP is out of scope |
| BAM pacing | 50 ms between TP.DT (spec allows 50–200 ms) |
| Receiver T1 | 750 ms inter-packet timeout → abort/discard |
| Sender T3 | 1250 ms wait for CTS / EndOfMsgAck → abort |
| CTS burst | grants up to 16 packets per CTS |
| TP priority | 7 |

## Tunables (Kconfig)

| Symbol | Default | Meaning |
|--------|---------|---------|
| `E32P_J1939TP_MAX_MSG_BYTES` | 1785 | bounds each session buffer |
| `E32P_J1939TP_MAX_RX_SESSIONS` | 2 | concurrent inbound transfers (keyed by source) |

## Testability

The unit suite runs two engines wired through an in-process queue with a virtual
clock — no bus needed — and proves: BAM round-trip reassembles byte-identical,
RTS/CTS multi-burst completes with `EndOfMsgAck`, a starved receiver aborts on
the T1 timeout, and `len < 9` is rejected. Crafted `TP.CM` / `TP.DT` frames show
that a BAM or RTS whose size and packet count disagree, or whose size is under 9
bytes, is rejected (no data delivered, RTS gets a `Conn_Abort`). They also show
the 9-byte boundary (RTS 9/3 rejected, BAM 9/2 accepted) and that the largest
legal message (1785 bytes, 255 packets) still reassembles intact.
See `test/` and `docs/TEST_PLAN.md`.

## Cherry-pick

```
CONFIG_E32P_J1939TP_ENABLE=y   # selects E32P_J1939_ENABLE (and TWAI)
```

Requires `e32p_core` and `e32p_j1939`.
