# e32p_uds

An ISO 14229-1 UDS server for the ESP32: diagnostic sessions, security access, data
identifiers, trouble codes, routines, and a download path that can reflash the device
over CAN. It runs on [`e32p_isotp`](../e32p_isotp) and can sit beside
[`e32p_obd2`](../e32p_obd2) on the same bus.

> **Status: Phase 3 done (2026-10-07).** Every service, the bus glue and the OTA reflash
> adapter are in; [example 33](../../examples/33_uds_server) puts it all on a bus and
> reflashes itself over CAN. The two-board HIL tests (`tests/hil/host/test_uds.py`) pass
> with 11-bit and 29-bit IDs, and so does udsoncan, a UDS client we did not write,
> through a CANable (`test_uds_canable.py`). **Not production-ready:** the security
> audit in `docs/SECURITY_HARDENING.md` has not been done. Plan and records:
> `docs/tickets/diagnostics-stack.md`, Phase 3.

## How it is built

The server is **pure**. Requests go in through `e32p_uds_request()`, time goes in
through `e32p_uds_poll(now_ms)`, and everything the server wants to say goes out
through a `send` callback you provide. It has no task, no timer and no bus of its own.

That shape is what lets it answer *later*. Erasing a firmware slot takes seconds, but
a UDS server must answer within 50 ms. So a slow callback returns
`E32P_UDS_NRC_PENDING`; the server tells the tester to wait (`7F <sid> 78`), keeps
telling it from `poll()`, and sends the real answer when you call
`e32p_uds_complete()`.

It also means every timing rule can be tested on a PC with a fake clock.

## On the bus

| | 11-bit | 29-bit |
|---|---|---|
| Requests to this ECU → its responses | `0x7E1` → `0x7E9` | `0x18DA11F1` → `0x18DAF111` |
| Broadcast requests | `0x7DF` | `0x18DB33F1` |

The ISO-TP glue that listens on these arrives in slice 3.5. OBD-II uses `0x7E0` →
`0x7E8`, so both servers can run at once.

## On the bus: the ISO-TP glue

[`e32p_uds_isotp.h`](include/e32p_uds_isotp.h) does the wiring for you: give it a server
config and it creates the server, its two ISO-TP channels and a task that runs it.

```c
e32p_uds_config_t server = { .dids = ..., .on_seed = ..., /* no send: the glue does it */ };
const e32p_uds_isotp_config_t glue = { .server = &server };
ESP_ERROR_CHECK(e32p_uds_isotp_start(&glue));   /* after e32p_isotp_twai_start() */
```

- **Every callback runs in the glue's task.** Slow work answers PENDING and later calls
  `e32p_uds_isotp_complete()` — from any task.
- **One server per broadcast address per link.** The ISO-TP link gives each receive ID to
  one channel, so if OBD-II already owns `0x7DF` on the same link, start UDS with
  `no_functional = true`.
- **Fast downloads by default.** The glue asks testers to send the frames of a long
  request back to back (ISO-TP STmin 0). On a 1 Mbit bus that made a reflash 8× faster
  than a 1 ms gap. If a receiver ever drops frames, set `rx_stmin_ms` (0–127 ms).

## Reflash: the OTA adapter

[`e32p_uds_ota.h`](include/e32p_uds_ota.h) gives you a sink and the `FF00` (erase) and
`FF01` (check) routines that write a downloaded image into the other firmware slot and
make it the boot image. Needs `CONFIG_E32P_OTA_ENABLE`. Without signed images, security
access is all that guards this path — see the security audit in
`docs/SECURITY_HARDENING.md`.

## Quick start (without the glue)

```c
static esp_err_t send(const uint8_t *data, uint16_t len, void *user)
{
    /* hand the message to your ISO-TP channel */
    return ESP_OK;
}

e32p_uds_config_t cfg = { .send = send };
e32p_uds_handle_t uds;
ESP_ERROR_CHECK(e32p_uds_init(&cfg, &uds));

/* for every reassembled request: */
e32p_uds_request(uds, E32P_UDS_ADDR_PHYSICAL, req, req_len, now_ms);

/* every few milliseconds: */
e32p_uds_poll(uds, now_ms);
```

## API

See [`include/e32p_uds.h`](include/e32p_uds.h). Each service is switched on by giving
the config what it needs, and stays "not supported" until you do:

| Give it | And it serves |
|---|---|
| `dids`, `on_did_read`, `on_did_write` | `0x22` read and `0x2E` write data by identifier |
| `on_seed`, `on_key` | `0x27` security access — your algorithm, your secret |
| `on_sec_load`, `on_sec_store` | optional: save "the last security access failed" so a reset cannot skip the lockout |
| `dtc` (a provider) | `0x19` read and `0x14` clear trouble codes |
| `routines`, `on_routine` | `0x31` routine control |
| `sink` | `0x34` / `0x36` / `0x37` download |
| `on_reset` | `0x11` ECU reset |

`0x10` session control and `0x3E` tester present need nothing.

## Kconfig

| Symbol | Default | Meaning |
|--------|---------|---------|
| `CONFIG_E32P_UDS_ENABLE` | n | build the component; selects `e32p_isotp` |

## Notes

- **No keys in this component.** Security access calls your `on_seed` and `on_key`.
  Keep the secret in NVS or secure storage, never in source.
- **Security access, level 1** (`27 01` seed, `27 02` key, 4 bytes each), only outside the
  default session. A seed answers one key. Three wrong keys in a row lock it out for 10
  seconds. Changing session locks it again — but does **not** reset the wrong-key count
  or the lockout, so switching sessions cannot be used to try keys faster.
- **A delay after a start, only without hooks or when the last access failed or is
  unknown.** After a reset or power-up the same 10 second delay runs (seed requests
  answer `0x37`) if the last access ended in a wrong key — or if the server cannot tell.
  Without `on_sec_load` it cannot tell, so the delay runs at every start. Give it a pair
  of hooks that keep one flag in NVS.
  - `on_sec_load` returns the flag. Return `true` if it is unreadable. A *missing* flag
    may be `false` only when missing can only mean that no access has failed since your
    storage was set up. That holds when wiping the storage also wipes the secret.
  - `on_sec_store` is called when the flag changes or the saved value is unknown:
    `true` after a wrong key, `false` after a right one. It returns `true` if it saved
    the value. A failed save makes the saved value unknown, so the next outcome saves
    again. A `true` from `on_sec_load` also makes the saved value unknown.
  - Set both hooks or neither.

  The delay starts at the first time value the server sees. The wrong-key count is not
  saved.
- **Trouble codes: your store only lists them.** The DTC provider gives a count and the
  code and status at each index; the server decides what matches a tester's status mask
  (ISO 14229-1's rule), and never reports a status bit outside your `availability_mask`.
  `0x19` supports report types `01`, `02` and `0A`; `0x14` clears everything (`FFFFFF`).
- **Routines** state their sessions and whether they need security in the table; the
  server checks both, then calls you with the option record (results, `03`, take none:
  extra bytes answer `0x13`). Start (`01`) and results
  (`03`) are offered — answer `0x24` yourself if results are asked for too early (the
  OTA adapter's routines do: results before a start answer `0x24`).
- **Slow work: answer `E32P_UDS_NRC_PENDING`, then call `e32p_uds_complete()`.** The
  server tells the tester to wait (`0x78`) at once and every 4 seconds, answers other
  requests "busy" (`0x21`) — except a service it does not offer, which stays `0x11`
  (and silent on a broadcast) — and gives up with `0x10` after `pending_max_ms` (60 s by
  default) if you never complete. Reads and seeds must answer at once.
- **Downloads go to your sink** (`begin`, `write`, `end`, `abort`). The server keeps the
  block counter and byte count; your sink only stores and, at `end`, checks the image.
  A failed write never advances the counter, so the tester can resend that block.
- **`send` must copy the bytes before returning** — the buffer is reused.
- **A reset callback runs after the response is handed to `send`** — but the
  transport may still be holding it. Restart only once it has left the wire.
- **Where it differs from the standard.** Ten known differences are listed, with the
  draft clause for each, in [`DESIGN.md`](DESIGN.md) Appendix A.

## Testing

`test/test_e32p_uds.c` is a Unity suite that needs no board, no bus and no waiting: every
message the server sends is captured, and time is whatever the test passes in. It
checks the exact bytes a tester would see, including the order in which the server
checks a request (ISO 14229-1 Figure 5), the S3 timeout to the millisecond, and that
`0x11` answers before it resets, and that the security lockout cannot be dodged by
switching sessions, and that a download survives lost answers and failed writes without
losing its place. There are 100 cases on the PC. The target's unit runner runs the same 100
plus 2 glue cases (the ISO-TP STmin check and the OTA routine case), 102 in all.

To run it on the PC in a few seconds: `python tests/host_unity/run_host_tests.py e32p_uds`.

## Cherry-pick

Copy `components/e32p_uds` with `e32p_core`, and `e32p_isotp` once the bus glue lands.
Enable `CONFIG_E32P_UDS_ENABLE`.
