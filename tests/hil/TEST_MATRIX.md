# HIL Test Matrix — coverage plan

The plan for "cover everything." Each feature group is tested **bottom-to-top**
across its layers, and **each layer** is exercised under five **categories**.
This is a living document: as tests are implemented, tick them and record the
`id` used in the agent registry.

## Categories (every layer gets these)

| # | Category | What it proves |
|---|----------|----------------|
| ① | **Functional** | happy path works end-to-end |
| ② | **Negative** | bad inputs, NULL, no-peer timeouts, malformed frames are handled |
| ③ | **Boundary** | max sizes, min sizes, queue-full, rapid re-init, off-by-one edges |
| ④ | **Stress / soak** | high rate, long duration; pass = 0 errors / N msgs **and** stable heap watermark |
| ⑤ | **Parallel** | multiple channels at once, high CPU + memory load, task contention |

## Rig requirement legend

- **1B** — one board (runs today).
- **2B** — two boards wired.
- **XCVR** — additionally needs CAN transceiver(s) + termination.
- **SLV** — additionally needs the deferred `e32p_spi_slave` / `e32p_i2c_slave`.

## Test-id convention

`<feature>_<layer>_<category><n>` — e.g. `can_tp_func1`, `spi_drv_neg2`,
`can_j1939_stress1`. Categories: `func` / `neg` / `bound` / `stress` / `par`.

---

## Feature group: CAN / J1939 (flagship, build first)

Layers: **L1** `e32p_twai` (raw frames) → **L2** `e32p_j1939` (codec + single frame)
→ **L3** `e32p_j1939tp` (transport).

| Layer | ① Functional | ② Negative | ③ Boundary | ④ Stress | ⑤ Parallel |
|-------|-------------|-----------|-----------|---------|-----------|
| L1 twai | frame TX↔RX between nodes (2B·XCVR) | RX timeout with no peer; bad bitrate (1B) | 0- and 8-byte DLC; extended vs std ID (2B·XCVR) | sustained max-rate frames, 0 loss (2B·XCVR) | TWAI + SPI active together (2B·XCVR) |
| L2 j1939 | PDU1/PDU2 single frame decode on peer (2B·XCVR) | non-J1939 frame rejected; SA guard (1B) | PGN edge values; PF=240 boundary on wire (2B·XCVR) | request/response flood (2B·XCVR) | — |
| L3 j1939tp | BAM + RTS/CTS 1785 B reassemble on peer (2B·XCVR) | T1 timeout abort; Conn_Abort mid-transfer (2B·XCVR) | 9 B (min) and 1785 B (max); 8 B rejected → single frame (2B·XCVR) | back-to-back multi-packet sessions, stable heap (2B·XCVR) | TP session while L1 frames also flow (2B·XCVR) |

*Note:* address claiming (`e32p_j1939_acl`, M7.3) becomes its own L2.5 row once built —
two nodes contend, lower NAME wins.

## Feature group: CAN diagnostics (ISO-TP, OBD-II, UDS)

Layers: **L1** `e32p_twai` → **L4** `e32p_isotp` (engine + TWAI link) → **L5** `e32p_obd2`
(SAE J1979 server) and `e32p_uds` (ISO 14229-1 server), both served by the companion.

| Category | Test id | Matrix id | Rig |
|----------|---------|-----------|-----|
| ① Functional | `isotp_self` — 100 B through two DUT channels looped by self-reception | `isotp_l4_func0` | 1B·XCVR |
| ① Functional | `isotp_sf` — 6 B single frame, echoed by the companion | `isotp_l4_func1` | 2B·XCVR |
| ① Functional | `isotp_mf` — 100 B, FF → FC → CF, echoed | `isotp_l4_func2` | 2B·XCVR |
| ① Functional | `isotp_fcwait` — the companion pauses the sender with FC.WAIT | `isotp_l4_func3` | 2B·XCVR |
| ② Negative | `isotp_ncr` — the companion abandons a response; the DUT discards it on N_Cr | `isotp_l4_neg1` | 2B·XCVR |
| ③ Boundary | `isotp_big` — 1024 B over several flow-control blocks | `isotp_l4_bound1` | 2B·XCVR |
| ⑤ Parallel / lifecycle | `isotp_lifecycle` — start/stop cycles leave no task or heap behind | `isotp_l4_par1` | 1B·XCVR |
| Interop | `test_canable_sniffs_ff_fc_cf`, `test_canable_isotp_peer` (host side) | `isotp_l4_interop1`, `_interop2` | 2B·XCVR·CANable |
| ① Functional | `obd2_pid_phys` — `01 0C` to 0x7E0 → `41 0C 1A F8` (1726 rpm) | `obd2_l5_func1` | 2B·XCVR |
| ① Functional | `obd2_pid_func` — `01 0D` on 0x7DF, answered from the ECU's own 0x7E8 | `obd2_l5_func2` | 2B·XCVR |
| ① Functional | `obd2_dtc_cycle` — seed P0133, read it, clear it, read nothing | `obd2_l5_func3` | 2B·XCVR |
| ① Functional | `obd2_freeze` — freeze frame stored by P0133 | `obd2_l5_func4` | 2B·XCVR |
| ② Negative | `obd2_unsupported` — `7F 01 12` physically, **silence** on the broadcast for a full N_Bs | `obd2_l5_neg1` | 2B·XCVR |
| ③ Boundary | `obd2_vin` — 20-byte VIN, a multi-frame response | `obd2_l5_bound1` | 2B·XCVR |
| Interop | `test_obd2_canable_pid_and_vin`, `test_obd2_canable_broadcast_and_silence` (host: the PC as scan tool over `can-isotp`) | `obd2_l5_interop1`, `_interop2` | 2B·XCVR·CANable |
| ① Functional | `uds_session` — `3E 80` keeps extended alive past 5 s; then `S3` falls back to default | `uds_l5_func1` | 2B·XCVR |
| ① Functional | `uds_security` — wrong key `0x35`, spent seed `0x24`, unlock, zero seed, relock on a session change | `uds_l5_func2` | 2B·XCVR |
| ① Functional | `uds_did_write` — needs extended (`0x31`) and security (`0x33`); written, read back; 20-byte VIN | `uds_l5_func3` | 2B·XCVR |
| ① Functional | `uds_dtc` — routine stores P0217, `19` lists it, OBD-II `03` sees it, `14` clears it for both | `uds_l5_func4` | 2B·XCVR |
| ① Functional | `uds_erase` — `7F 31 78` within P2, `0x21` meanwhile, result after 2 s | `uds_l5_func5` | 2B·XCVR |
| ① Functional | `uds_reset` + `uds_after_reset` — `51 01`, the companion really restarts; a RAM value proves it | `uds_l5_func6` | 2B·XCVR |
| ② Negative | `uds_functional` — broadcast `3E 00` answered; `3E 80` and an unknown service silent on broadcast | `uds_l5_neg1` | 2B·XCVR |
| ② Negative | `uds_lockout` — third wrong key `0x36`, `0x37` across a session change, unlocked after 10 s | `uds_l5_neg2` | 2B·XCVR |
| ③ Boundary | `uds_download` — 4 KB in four 1026-byte (maximum) blocks, CRC-32 match and a wrong one caught | `uds_l5_bound1` | 2B·XCVR |
| Interop | `test_uds_canable_full_flow`, `test_uds_canable_follows_response_pending` (host: udsoncan as the tester; a 7 s erase only passes if the `0x78` repeats inside P2*) | `uds_l5_interop1`, `_interop2` | 2B·XCVR·CANable |

Protocol corner cases that need no bus — the ISO-TP engine's, the OBD-II service engine's and the UDS server's Unity suites — are A-01 in `docs/TEST_PLAN.md`.

## Feature group: SPI

Layers: **L1** `e32p_spi` driver → **L2** device protocol (register read/write).

| Layer | ① Functional | ② Negative | ③ Boundary | ④ Stress | ⑤ Parallel |
|-------|-------------|-----------|-----------|---------|-----------|
| L1 | master↔slave transfer, byte-match (2B·SLV) | bad host; NULL bufs (1B) | max transfer size; mode 0–3 (2B·SLV) | sustained transfers, 0 mismatch (2B·SLV) | SPI + UART + CAN at once (2B·SLV·XCVR) |

## Feature group: I²C

Layers: **L1** `e32p_i2c` driver → **L2** register helpers.

| Layer | ① Functional | ② Negative | ③ Boundary | ④ Stress | ⑤ Parallel |
|-------|-------------|-----------|-----------|---------|-----------|
| L1 | master read/write to slave; scan finds it (2B·SLV) | probe absent addr → NOT_FOUND; NACK (1B/2B) | 0-len and max-len; clock stretch (2B·SLV) | rapid transactions, 0 error (2B·SLV) | I²C + CAN parallel (2B·SLV·XCVR) |

## Feature group: UART

Layers: **L1** `e32p_uart` driver.

| Layer | ① Functional | ② Negative | ③ Boundary | ④ Stress | ⑤ Parallel |
|-------|-------------|-----------|-----------|---------|-----------|
| L1 | cross-wired TX/RX between boards (2B) · internal loopback (1B) | read timeout, no peer (1B) | RX ring overflow; max baud (2B) | continuous stream, 0 loss (2B) | UART + SPI parallel (2B·SLV) |

## Feature group: Wireless (Wi-Fi / ESP-NOW)

Layers: **L1** `e32p_wifi` → **L2** `e32p_espnow`.

| Layer | ① Functional | ② Negative | ③ Boundary | ④ Stress | ⑤ Parallel |
|-------|-------------|-----------|-----------|---------|-----------|
| L1 wifi | STA connects to AP; SoftAP accepts a client (2B) | wrong password → FAIL; timeout (1B/2B) | connect/disconnect churn (2B) | reconnect soak (2B) | — |
| L2 espnow | A→B unicast + broadcast received (2B) | send before init; >250 B rejected (1B) | 250 B max payload; many peers (2B) | high-rate messaging, stable heap (2B) | ESP-NOW while CAN flows (2B·XCVR) |

## Feature group: GPIO / analog / timers (mostly 1-board)

| Feature | ① Functional | ② Negative | ③ Boundary | ④ Stress |
|---------|-------------|-----------|-----------|---------|
| gpio | drive + read back (loopback wire, 2B or 1B jumper) | reserved/flash pin refused (1B) | strapping-pin warnings (1B) | fast toggle soak (1B) |
| ledc | duty/fade observed via ADC (1B wire) | invalid pin/resolution (1B) | 0 % / 100 % duty (1B) | — |
| adc | read a known divider (1B) | ADC2-while-WiFi conflict (1B) | full-scale / zero (1B) | — |
| pcnt | count generated pulses (1B loopback) | bad args (1B) | high/low limit watch-points (1B) | high-freq counting (1B) |
| gptimer | alarm period accuracy (1B) | bad args (1B) | very short / long periods (1B) | — |

## System-wide

| Test | Category | Rig | Notes |
|------|----------|-----|-------|
| `sys_smoke` | ① | 1B | PING + SYSINFO (implemented) |
| `sys_heap_soak` | ④ | 1B | run all tests in a loop; heap watermark must not drift |
| `sys_parallel_load` | ⑤ | 2B·XCVR·SLV | CAN + SPI + UART + ESP-NOW simultaneously under CPU load |
| `sys_reset_recovery` | ② | 1B | reset reason correct after WDT/panic path |

---

## Phasing

0. **Framework** (this skeleton) — agent + protocol + PC harness + `sys_smoke`.  ← current
1. **CAN/J1939 group** (2B·XCVR) — L1→L2→L3, all five categories. Flagship.
2. **UART + ESP-NOW** (2B, no extra parts) — symmetric peers, no slave drivers needed.
3. **Negative/boundary sweep** across implemented groups.
4. **SPI/I²C** — after `e32p_spi_slave` / `e32p_i2c_slave` land.
5. **Stress + parallel** — throughput thresholds, soak, multi-channel load.

Determinism rule: every ④/⑤ test must define explicit numeric pass criteria
(throughput ≥ X, 0 errors / N, heap drift = 0) — no "looks fine" tests.

---

## Implemented test IDs (2026-08-13)

Registered in the agent; driven by the matching `host/test_*.py`. Compile-verified;
runtime verification deferred to the bench (single-board ones can be run today).

| Group | Single board (run now) | Two board / deferred |
|-------|------------------------|----------------------|
| System | `sys_event`, `sys_nvs`, `sys_wdt`, `sysinfo` (smoke), `pm_wakecause`, `pm_lightsleep`, `ota_query` | OTA image swap + rollback (25_ota demo, reboots) |
| GPIO/analog/timer | `periph_gpio`, `periph_ledc`, `periph_adc`, `periph_dac`, `periph_touch`, `periph_pcnt`, `periph_gptimer` | — |
| Buses | `bus_uart_loop`, `bus_i2c_bus`, `bus_spi_loop`, `bus_i2s_tx`, `bus_ws2812` | `bus_uart_xfer` (2B); `bus_spi_xfer`, `bus_i2c_xfer` (needs slave drivers) |
| Wireless | `wl_wifi_neg`, `wl_espnow_neg`, `mqtt_selftest`, `ble_selftest` | `wl_espnow_xfer` (2B); BLE peripheral connect (27_ble demo, needs a central/phone) |
| CAN/J1939 | `can_twai_neg`, `can_twai_selftx`, `can_j1939_codec`, `can_j1939_neg`, `can_tp_loop`, `can_tp_neg`, `can_acl_contend` | `can_twai_xfer`, `can_j1939_single`, `can_j1939_rx`, `can_tp_bam`, `can_tp_cmdt`, `can_stress` — **all PASS 2026-08-28 on WROOM↔S3 (2B·XCVR)** |
| CAN diagnostics (ISO-TP) | `isotp_self`, `isotp_lifecycle` (need the transceiver) | `isotp_sf`, `isotp_mf`, `isotp_big`, `isotp_fcwait`, `isotp_ncr` — **all 7 PASS 2026-09-15 on WROOM↔S3 at 250 kbit, twice**; CANable interop in `test_isotp_canable.py` (`--canable`) — **2/2 PASS at 250 k, 500 k, 1 M and 29-bit** |
| CAN diagnostics (OBD-II) | — | `obd2_pid_phys`, `obd2_pid_func`, `obd2_unsupported`, `obd2_vin`, `obd2_dtc_cycle`, `obd2_freeze` (companion serves; `obd2_dtc_seed` runs on the companion) — **all 6 PASS — 11-bit 2026-10-02, 29-bit 2026-10-05**; CANable interop in `test_obd2_canable.py` — **2/2 PASS, both addressing modes** |
| CAN diagnostics (UDS) | — | `uds_session`, `uds_functional`, `uds_security`, `uds_did_write`, `uds_dtc`, `uds_erase`, `uds_download`, `uds_lockout`, `uds_reset` + `uds_after_reset` (companion serves and really restarts on `11 01`) — **all PASS — 11-bit 2026-10-06, 29-bit 2026-10-07, full suite twice each**; udsoncan interop in `test_uds_canable.py` — **2/2 PASS, both addressing modes** |
| Serial protocol | `modbus_selftest` | `modbus_rtu_xfer` (2B, DUT master ↔ companion slave over crossed UART2, 9600 8E1) |
| Device drivers | — | `sensor_bmp280`, `sensor_tm1637` (needs the part wired; `--with-sensors`) |
| Network (host = broker/server) | — | `test_net_mqtt` (broker round-trip), `test_net_ota` (HTTP OTA + slot flip) — opt-in `--wifi-ssid` |

Every one of the 30 modules now has at least one HIL test. `RUN <id>` on the agent
lists diagnostics (heap delta + timing); `@CMD LIST` enumerates all registered ids.

**Opt-in tests** (skipped by default so a bare `pytest --port COM5` stays clean):
- **Device drivers** — `--with-sensors` (BMP280 on I2C SDA21/SCL22, TM1637 on CLK18/DIO19).
- **Network** — set `$env:HIL_WIFI_SSID = "<ssid>"` and `$env:HIL_WIFI_PASS = "<password>"`
  first (the password has no command-line option; see `README.md`). The PC acts as the MQTT
  broker (drives a local Mosquitto) and the OTA file server (`http.server`); the
  agent joins Wi-Fi via `@CMD WIFI` and connects back. These automate the manual
  Mosquitto / `python -m http.server` bench procedures — the agent gains the
  `WIFI` / `MQTT` / `OTA` / `OTASLOT` commands, and the OTA test reboots the board
  and asserts the slot flipped. Needs `paho-mqtt` (`pip install -r requirements.txt`),
  Mosquitto installed, and inbound TCP 1883 + `--http-port` (8080) allowed through
  the firewall.

---

## Regression after the security remediation (2026-11-03)

The full suite was run again after slices A to J of the security remediation. Same rig as
the baseline (WROOM-32 DUT on COM5, ESP32-S3 companion on COM7, CANable on COM8 for the
`*_canable` tests).

| Addressing | Images (DUT / companion) | Result |
|------------|--------------------------|--------|
| 11-bit normal | `build` / `build_esp32s3` | **77 passed, 4 skipped** (98.86 s) |
| 29-bit normal-fixed (`--isotp-addr normfixed`) | `build_nf` / `build_s3_nf` | **77 passed, 4 skipped** (99.01 s) |

What the 4 skips are, in both runs:

| Skipped test | Matrix rows it covers | Why it skips |
|--------------|-----------------------|--------------|
| `test_bus.py` SPI slave | SPI two-board transfer (`bus_spi_xfer`) | needs the deferred `e32p_spi_slave` driver |
| `test_bus.py` I²C slave | I²C two-board transfer (`bus_i2c_xfer`) | needs the deferred `e32p_i2c_slave` driver |
| `test_net_mqtt.py` | Network, MQTT broker round-trip | the env vars `HIL_WIFI_SSID` and `HIL_WIFI_PASS` are not set |
| `test_net_ota.py` | Network, HTTP OTA and slot flip | the env vars `HIL_WIFI_SSID` and `HIL_WIFI_PASS` are not set |

The two Wi-Fi tests were therefore **not exercised** in this regression. They run when
the owner sets `HIL_WIFI_SSID` and `HIL_WIFI_PASS` (see "Opt-in tests" above); until
then they stay pending.

Run `pytest` from a shell without the ESP-IDF environment, with the global Python
(the global Python 3.12, `...\Programs\Python\Python312\python.exe`; it has `udsoncan`,
the `.venv` does not). Details and the build sizes: `tests/hil/BASELINE.md`, section
"Security remediation, slices A-J".
