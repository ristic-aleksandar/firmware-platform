# e32p_j1939 — SAE J1939 protocol layer

J1939 protocol/data layer over [`e32p_twai`](../e32p_twai). It is the 29-bit CAN
ID ↔ J1939 field codec plus single-frame (≤8 byte) send/receive. Multi-packet
messages (9–1785 bytes) are handled by `e32p_j1939tp`; address claiming
(J1939-81) by `e32p_j1939_acl`.

## The 29-bit identifier

```
 28    26 25  24 23        16 15        8 7         0
+--------+---+---+------------+-----------+-----------+
|Priority|EDP| DP| PDU Format |PDU Specif.|  Src Addr |
+--------+---+---+------------+-----------+-----------+
```

- **PDU1** (PF < 240): destination-specific. `PS` = destination address, **not**
  part of the PGN.
- **PDU2** (PF ≥ 240): broadcast. `PS` = group extension, **part** of the PGN;
  destination is implicitly global (0xFF).

The PGN (18 bits) is `EDP·DP·PF·PS` with `PS` forced to 0 for PDU1.

## API

```c
/* Pure codec — no hardware */
uint32_t e32p_j1939_id_encode(uint8_t priority, uint32_t pgn, uint8_t src, uint8_t dst);
void     e32p_j1939_id_decode(uint32_t id, uint8_t *priority, uint32_t *pgn,
                              uint8_t *src, uint8_t *dst);
bool     e32p_j1939_pgn_is_pdu1(uint32_t pgn);

/* Node + single-frame transfer (TWAI must be init+started first) */
esp_err_t e32p_j1939_init(const e32p_j1939_config_t *cfg);
esp_err_t e32p_j1939_deinit(void);
esp_err_t e32p_j1939_send(const e32p_j1939_message_t *msg, uint32_t timeout_ms);
esp_err_t e32p_j1939_receive(e32p_j1939_message_t *msg, uint32_t timeout_ms);
esp_err_t e32p_j1939_request(uint32_t requested_pgn, uint8_t dst, uint32_t timeout_ms);
```

## Usage

```c
/* 1) Bring up the bus (pins/bitrate/mode live in e32p_twai). */
e32p_twai_config_t bus = { .tx_gpio = 21, .rx_gpio = 22,
                           .bitrate_bps = 250000, .mode = E32P_TWAI_MODE_NORMAL };
ESP_ERROR_CHECK(e32p_twai_init(&bus));
ESP_ERROR_CHECK(e32p_twai_start());

/* 2) Bind this node's address. */
e32p_j1939_config_t node = { .src_addr = 0x80, .default_priority = 6 };
ESP_ERROR_CHECK(e32p_j1939_init(&node));

/* 3) Broadcast a PDU2 message. */
e32p_j1939_message_t msg = { .priority = 0xFF /* default */, .pgn = 65262 /*0xFEEE*/,
                             .dst_addr = E32P_J1939_ADDR_GLOBAL, .len = 8, .data = {0} };
ESP_ERROR_CHECK(e32p_j1939_send(&msg, 100));
```

## Notes

| Topic | Detail |
|-------|--------|
| Bus config | Pins, bitrate (250 k default), and mode belong to `e32p_twai`. This layer only adds the node address and framing. |
| Source address | `send` always uses the configured node SA. For full ID control, use `e32p_j1939_id_encode` + `e32p_twai` directly. |
| Self-reception | Set `cfg.self_reception = true` **only** for single-board loopback with TWAI in `NO_ACK` self-test mode. On a real bus keep it false — a node must not receive its own frames. |
| Testability | The codec is pure and fully unit-tested (PDU1/PDU2, the PF=240 boundary, a known real ID). A round-trip test uses NO_ACK self-reception on one board. |
| Real bus | Needs an external CAN transceiver (e.g. SN65HVD230) and at least one peer. |

## Cherry-pick

```
CONFIG_E32P_J1939_ENABLE=y   # selects E32P_TWAI_ENABLE
```

Requires `e32p_core`; privately requires `e32p_twai`.
