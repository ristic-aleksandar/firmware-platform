# e32p_isotp

ISO 15765-2 (ISO-TP) transport over classic CAN. Segments and reassembles
payloads of 1–4095 bytes using SF / FF / CF / FC. This is the transport OBD-II
and UDS run on.

> **Not J1939/TP.** `e32p_j1939tp` is the SAE J1939-21 BAM/CMDT transport — a
> different segmentation scheme, different addressing, different consumers. The
> two coexist on one bus (see *Sharing a bus with J1939* below).

## Two layers

| Layer | Header | What it is |
|-------|--------|------------|
| **Engine** | `e32p_isotp.h` | All the protocol. Pure: no TWAI, no FreeRTOS, no clock. Frames leave through an injected sink, arrive via `on_frame()`, time arrives via `poll(now_ms)`. |
| **Link** | `e32p_isotp_twai.h` | Binds to the single TWAI controller: one pump task, ID routing, blocking `send_sync` / `recv_sync`. |

The split is why the engine is testable headless — two engines wired through an
in-process queue with a virtual clock, no bus, no board. Same shape as
`e32p_j1939tp`.

## Quick start — blocking

```c
e32p_twai_config_t bus = { .tx_gpio = 21, .rx_gpio = 22,
                           .bitrate_bps = E32P_ISOTP_DEFAULT_BITRATE,
                           .mode = E32P_TWAI_MODE_NORMAL };
ESP_ERROR_CHECK(e32p_twai_init(&bus));
ESP_ERROR_CHECK(e32p_twai_start());
ESP_ERROR_CHECK(e32p_isotp_twai_start(NULL));

e32p_isotp_config_t cfg;
e32p_isotp_config_defaults(&cfg, /*we send on*/0x7E0, /*we listen on*/0x7E8,
                           /*sa*/0xF1, /*ta*/0x10, /*functional*/false);
e32p_isotp_handle_t ch;
ESP_ERROR_CHECK(e32p_isotp_twai_channel(&cfg, &ch));   /* on_rx NULL -> sync */

uint8_t req[100] = { ... };
ESP_ERROR_CHECK(e32p_isotp_twai_send_sync(ch, req, sizeof(req), 2000));

uint8_t rsp[512]; uint16_t n;
if (e32p_isotp_twai_recv_sync(ch, rsp, sizeof(rsp), &n, 2000) == ESP_OK) {
    ESP_LOGI(TAG, "got %u bytes", n);
}

e32p_isotp_twai_destroy_channel(ch);
e32p_isotp_twai_stop();          /* MUST precede the two lines below */
e32p_twai_stop();
e32p_twai_deinit();
```

## Quick start — server (OBD-II / UDS shape)

Set `cfg.on_rx` and the callback fires from the pump task as each request
completes. Reply with `e32p_isotp_send()` — **not** `send_sync`, which would
deadlock waiting on the pump from inside it.

```c
static void on_request(const uint8_t *req, uint16_t len, void *user)
{
    uint8_t rsp[8];
    uint16_t n = build_response(req, len, rsp);
    e32p_isotp_send((e32p_isotp_handle_t)user, rsp, n);   /* arms and returns */
}
```

## API

| Function | Purpose |
|---|---|
| `e32p_isotp_config_defaults` | Fill a config with Kconfig addressing + ISO timer defaults |
| `e32p_isotp_init` / `_deinit` | Create / destroy an engine channel |
| `e32p_isotp_send` | Start a transfer; non-blocking, result via `on_tx_done` |
| `e32p_isotp_on_frame` | Feed one received CAN frame |
| `e32p_isotp_poll` | Service timers, pace consecutive frames |
| `e32p_isotp_tx_busy` / `_rx_busy` / `_rx_id` / `_is_extended` | State queries |
| `e32p_isotp_pci_*`, `_stmin_us`, `_id_normal_fixed` | Pure codec helpers, also useful for tooling |
| `e32p_isotp_twai_start` / `_stop` / `_running` | Pump lifecycle |
| `e32p_isotp_twai_channel` / `_destroy_channel` | Channel bound to TWAI |
| `e32p_isotp_twai_attach` / `_detach` | Route (or stop routing) an existing engine |
| `e32p_isotp_twai_send_sync` / `_recv_sync` | Blocking helpers |

## Kconfig

| Symbol | Default | Notes |
|---|---|---|
| `E32P_ISOTP_ENABLE` | n | Selects `E32P_TWAI_ENABLE` |
| `E32P_ISOTP_BITRATE` | 250 kbit | Choice: 250 k / 500 k / 1 M. Surfaces as `E32P_ISOTP_DEFAULT_BITRATE` |
| `E32P_ISOTP_ADDRESSING` | Normal 11-bit | Choice: normal 11-bit / normal-fixed 29-bit |
| `E32P_ISOTP_MAX_MSG_BYTES` | 1024 | Two buffers per channel, so RAM = 2 × this |
| `E32P_ISOTP_MAX_CHANNELS` | 4 | An OBD server needs two (0x7DF functional + 0x7E0 physical) |

**The engine reads neither choice.** They are defaults for application and test
code, consumed through `e32p_isotp_config_defaults()`; every field stays
overridable at runtime. A CAN bus is a single-bitrate domain, so changing the
bitrate means rebuilding and reflashing *every* node, sniffers included.

## Notes / gotchas

| Item | Detail |
|---|---|
| **Teardown order** | `e32p_isotp_twai_stop()` **before** `e32p_twai_deinit()`. Deinit deletes the RX queue; a pump still blocked on it is undefined behaviour, not just a leak. `_stop()` hard-joins the task before returning. |
| **`send_sync` from a callback** | Self-deadlock — callbacks run in the pump task. Rejected with `ESP_ERR_INVALID_STATE` rather than hanging. Use `e32p_isotp_send()`. |
| **STmin pacing** | `poll()` only sees whole milliseconds, so CFs go out **STmin + 1 ms** apart: at STmin 1 ms that is one CF every 2 ms. Sub-ms values `0xF1`–`0xF9` (100–900 µs) count as 1 ms. Sending slower than the peer asked is always spec-legal. An idle pump polls once per tick, so with `CONFIG_FREERTOS_HZ` below 1000 the gap grows to the tick period. |
| **STmin = 0** | Unaffected by tick rate. `poll()` emits as many CFs as block size and the TX queue allow, so throughput is queue-bound, not poll-bound. Raise `E32P_TWAI_TX_QUEUE_LEN` for high bitrates. |
| **Concurrency** | One TX **and** one RX session may run at once on a channel — different IDs, opposite directions. Two TX sessions is `ESP_ERR_INVALID_STATE`; a second FF restarts reassembly. |
| **Padding** | `pad_tx` / `pad_byte`, default `0xAA`. A peer's *unpadded* frames (DLC = actual length) are accepted either way. |
| **Threading** | One handle is driven from one task. The link serialises engine access with a recursive mutex, so `send_sync` from another task is safe. |
| **Addressing** | Normal 11-bit and normal-fixed 29-bit. Extended and mixed addressing are out of scope in v1. Classic CAN only — no CAN-FD, and `FF_DL == 0` (the 32-bit escape) is rejected rather than misparsed. |

## Sharing a bus with J1939

Normal-fixed ISO-TP composes PF **0xDA** (physical) / **0xDB** (functional) —
PGN 55808 / 56064, which **SAE J1939-73 reserves for exactly ISO 15765-2**. Those
are disjoint from every PGN this platform uses (`0xEA 0xEB 0xEC 0xEE 0xF0 0xFE`),
so a J1939 stack and a diagnostics stack coexist on one wire. In 11-bit mode the
separation is trivial, since J1939 is 29-bit only.

## Testing

`test/test_e32p_isotp.c` — 33 Unity cases, zero hardware: PCI codec, four
round-trips (SF, 100 B, 4095 B, unpadded peer), flow control both directions,
N_Bs / N_Cr expiry, sequence errors, BS=0 and STmin=0, STmin pacing after a late
Flow Control, CF batching, concurrent TX+RX, and sink back-pressure. Run them
through `tests/unit_runner`.

## Cherry-pick

Copy `components/e32p_isotp/` and `components/e32p_twai/`, plus `e32p_core`. Set
`CONFIG_E32P_ISOTP_ENABLE=y`. The link half additionally needs `esp_timer`
(private). Taking only `src/e32p_isotp.c` and its header gives you the protocol
with no ESP-IDF dependency beyond `esp_err_t` — it will compile against any
CAN driver you hand it a sink for.
