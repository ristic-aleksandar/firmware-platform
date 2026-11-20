# e32p_obd2

OBD-II services (SAE J1979 / ISO 15765-4) on top of `e32p_isotp`: the ESP32
answers a scan tool like an engine ECU.

> **Status: on the bus (ticket slice 2.3).** All six services, the NVS-backed
> trouble-code store, and the ISO-TP glue work; example 32 reads live values, the
> VIN and a stored code between two boards over real CAN. HIL tests (2.4) and the
> PC lane (2.5) are next. Plan: `docs/tickets/diagnostics-stack.md`, Phase 2.

## Two layers

| Layer | File | Needs |
|-------|------|-------|
| Services | `src/e32p_obd2.c`, `src/e32p_obd2_dtc.c` | nothing — no bus, no task, no clock |
| Transport glue | `src/e32p_obd2_isotp.c` | `e32p_isotp` link, two channels |

The service layer takes one reassembled request and fills one response buffer.
That is the whole contract, which is why it is testable headless.

## On the bus

```c
/* TWAI up, ISO-TP link started, then: */
const e32p_obd2_isotp_config_t bind = { .server = srv };   /* ECU 0x10 = 0x7E0 / 0x7E8 */
ESP_ERROR_CHECK(e32p_obd2_isotp_start(&bind));
```

Two channels, because a broadcast (`0x7DF`) and a physical request (`0x7E0`) can be
in flight at once and each channel owns one reassembly buffer. Only the physical
channel transmits: a broadcast is answered from the ECU's own ID, which is what
sending on the physical channel does. A response that finds the transmitter still
busy is dropped and counted by `e32p_obd2_isotp_dropped()` — a scan tool asks one
question at a time, so a non-zero count means a tester overlapped requests.
`examples/32_obd2_ecu` shows the whole thing, ECU and tester.

## Quick start

```c
static const e32p_obd2_pid_t pids[] = {
    { 0x05, 1 },    /* coolant temperature */
    { 0x0C, 2 },    /* engine RPM          */
    { 0x0D, 1 },    /* vehicle speed       */
};

static esp_err_t read_pid(uint8_t pid, uint8_t *out, uint8_t len, void *user)
{
    const sim_t *s = user;
    switch (pid) {
    case 0x05: out[0] = (uint8_t)(s->coolant_c + 40.0f);            return ESP_OK;
    case 0x0C: { uint16_t q = (uint16_t)(s->rpm * 4.0f);
                 out[0] = q >> 8; out[1] = q & 0xFF;               return ESP_OK; }
    case 0x0D: out[0] = (uint8_t)s->speed_kph;                      return ESP_OK;
    default:                                                        return ESP_FAIL;
    }
}

e32p_obd2_config_t cfg = {
    .pids = pids, .pid_count = 3, .on_read = read_pid, .user = &sim,
    .vin = "E32PDEMO000000001", .obd_standard = 0x06, .nvs_namespace = "obd2",
};
e32p_obd2_handle_t srv;
ESP_ERROR_CHECK(e32p_obd2_init(&cfg, &srv));

uint8_t rsp[E32P_OBD2_RESP_MAX];
uint16_t n = 0;
e32p_obd2_request(srv, E32P_OBD2_ADDR_PHYSICAL, req, req_len, rsp, sizeof(rsp), &n);
if (n > 0) { send(rsp, n); }      /* n == 0 means: stay silent */
```

## API

| Function | Purpose |
|---|---|
| `e32p_obd2_init` / `_deinit` | Create a server from a PID table, a value provider, a VIN and a DTC namespace |
| `e32p_obd2_request` | Answer one request; `resp_len == 0` means send nothing |
| `e32p_obd2_dtc_set` | Store a code, e.g. `0x0133` for P0133 (slice 2.2) |
| `e32p_obd2_dtc_clear` | Erase stored, pending and freeze-frame data — what mode `0x04` does |
| `e32p_obd2_dtc_count` | How many codes are stored, confirmed or pending |
| `e32p_obd2_dtc_get` | Read one stored code by position — lets UDS show the same codes |

## Kconfig

| Symbol | Default | Meaning |
|---|---|---|
| `E32P_OBD2_ENABLE` | n | Build the component; selects `E32P_ISOTP_ENABLE` |
| `E32P_OBD2_MAX_DTC` | 10 | Codes the store holds; 3 bytes of RAM each |

## Notes / gotchas

| Item | Detail |
|---|---|
| **Silence is a response** | A *functional* request (`0x7DF`) this ECU cannot answer gets **nothing back** — ISO 15765-4, so a broadcast is not answered by every ECU with an error. A *physical* request (`0x7E0`) gets a negative response. `resp_len == 0` is how the layer says "stay silent". |
| **Supported-PID bitmasks are derived** | PID `0x00` (and `0x20`) are computed from the table, never hand-maintained, so they cannot disagree with it. `0x20` is only advertised when a PID above it exists. |
| **Both requests answer on the physical ID** | A functional request is answered from `0x7E8`, this ECU's own response ID, not from a broadcast ID. |
| **The value provider must not block** | `on_read` runs while the response is being built, in the transport's context. Read a cached value. |
| **The response buffer is 68 bytes** | Not 20: a full trouble-code list is `0x43`, a count and two bytes per code — 66 bytes at the largest store. `e32p_obd2_request()` refuses a smaller buffer rather than truncating a response onto the bus. |
| **Codes persist, the freeze frame does not** | Codes go to one short NVS string, written only when one is set or the store is cleared. The freeze frame holds live sensor values, so it is not restored after a reset — mode `0x02` answers "not supported" until a new code appears. |
| **A damaged store costs codes, not the boot** | Parsing the NVS string stops at the first entry that does not decode and keeps what came before it. |
| **Addressing is the transport's business** | 11-bit and 29-bit are chosen in `e32p_isotp`'s Kconfig; this layer never sees a CAN ID. |

## Testing

`test/test_e32p_obd2.c` — 26 Unity cases, zero hardware: configuration rejection,
the derived bitmask (including "the next block exists"), each PID's encoding, a
provider that refuses, more than one PID per request, the silence-versus-negative
rule, unknown services, the VIN, both code lists and their count byte, clearing,
the store's limit, the freeze frame (including that it keeps the values from when
the code appeared, and that only frame 0 exists), and persistence. Run them
through `tests/unit_runner`.

The persistence case runs against real NVS on the target and erases its own key,
so repeated runs stay identical.

## Cherry-pick

Copy `components/e32p_obd2/` plus `e32p_isotp`, `e32p_twai`, `e32p_nvs` and
`e32p_core`. Set `CONFIG_E32P_OBD2_ENABLE=y`.
