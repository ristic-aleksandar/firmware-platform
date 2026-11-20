# e32p_j1939_acl — J1939-81 Address Claiming

Lets a J1939 node claim and defend a source address on the bus, on top of
[`e32p_j1939`](../e32p_j1939). Completes the CAN stack's network-management layer
(the "static SA vs. address claiming" choice): a compliant node uses this to
establish its address dynamically; a closed-network node can skip it and stay
static.

## What it does

- **Packs/unpacks the 64-bit NAME** (J1939-81) from its eight fields.
- **Claims** `preferred_sa` by broadcasting *Address Claimed* (PGN 60928).
- **Arbitrates** contention: when two nodes want the same address, the
  **numerically lower NAME wins**.
- **Defends**: re-sends its claim when a higher-NAME node contests.
- On a **lost** claim: **relocates** to a free self-configurable address
  (128–247) if arbitrary-address-capable, else sends *Cannot Claim* (SA = 254).
- **Answers** Requests (PGN 59904) for Address Claimed.

## Design

Same injected-sink + `poll(now_ms)` tick as `e32p_j1939tp` — decoupled from bus
and clock, so it is fully unit-testable headless (two engines contend over an
in-process queue). Handle-based.

## API

```c
uint64_t  e32p_j1939_name_encode(const e32p_j1939_name_t *name);
void      e32p_j1939_name_decode(uint64_t value, e32p_j1939_name_t *out);

esp_err_t e32p_j1939_acl_init(const e32p_j1939_acl_config_t *cfg, e32p_j1939_acl_handle_t *out);
esp_err_t e32p_j1939_acl_deinit(e32p_j1939_acl_handle_t h);
esp_err_t e32p_j1939_acl_start(e32p_j1939_acl_handle_t h);
esp_err_t e32p_j1939_acl_on_frame(e32p_j1939_acl_handle_t h, const e32p_j1939_message_t *f);
void      e32p_j1939_acl_poll(e32p_j1939_acl_handle_t h, uint32_t now_ms);
uint8_t   e32p_j1939_acl_current_sa(e32p_j1939_acl_handle_t h);
bool      e32p_j1939_acl_is_claimed(e32p_j1939_acl_handle_t h);
```

## Usage

```c
e32p_j1939_name_t name = { .arbitrary_address_capable = true, .function = 130,
                           .manufacturer_code = 1234, .identity_number = 0x1ABCD };
e32p_j1939_acl_config_t cfg = { .name = e32p_j1939_name_encode(&name),
                                .preferred_sa = 0x80, .tx = acl_tx, .on_event = on_addr };
e32p_j1939_acl_handle_t acl;
e32p_j1939_acl_init(&cfg, &acl);
e32p_j1939_acl_start(acl);

/* In the CAN task loop: feed Address-Claimed/Request frames + tick. */
if (e32p_j1939_receive(&m, 10) == ESP_OK) e32p_j1939_acl_on_frame(acl, &m);
e32p_j1939_acl_poll(acl, esp_timer_get_time() / 1000);
/* When on_event(sa, true) fires, use sa as the node's e32p_j1939 source address. */
```

The `tx` sink builds the CAN frame from the message's **explicit** `src_addr`
(it changes across a claim), e.g. `e32p_j1939_id_encode(...) → e32p_twai_transmit`.

## Protocol details

| Item | Value |
|------|-------|
| Address Claimed | PGN 60928 (0xEE00), priority 6, 8-byte NAME little-endian, dst global |
| Request | PGN 59904 (0xEA00), 3-byte requested-PGN payload |
| Cannot Claim | Address Claimed with SA = 254 (null) |
| Contention window | Tc = 250 ms |
| Relocation range | 128–247 (self-configurable addresses) |
| Arbitration | lower 64-bit NAME wins |

## NAME bit layout (J1939-81)

| Bits | Field |
|------|-------|
| 63 | Arbitrary Address Capable |
| 62–60 | Industry Group |
| 59–56 | Vehicle System Instance |
| 55–49 | Vehicle System |
| 48 | Reserved (0) |
| 47–40 | Function |
| 39–35 | Function Instance |
| 34–32 | ECU Instance |
| 31–21 | Manufacturer Code |
| 20–0 | Identity Number |

## Cherry-pick

```
CONFIG_E32P_J1939_ACL_ENABLE=y   # selects E32P_J1939_ENABLE (and TWAI)
```

Requires `e32p_core` and `e32p_j1939`.
