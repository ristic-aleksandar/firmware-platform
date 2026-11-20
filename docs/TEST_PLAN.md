# ESP32 Platform — Follow-up Test Plan

Living document. It captures verification that the build gate and the current
on-device demos do **not** cover — mostly things needing external hardware,
fault injection, instruments, or the not-yet-built Unity test harness. We append
to it as modules are added.

## How this document is organized

Tests are split into three groups by **execution complexity** (how hard the test
is to run), and inside each group they are ordered by **priority** (most
important first).

**Groups (execution complexity)**
- **Basic** — flash an existing example and read the serial log. No extra
  hardware, no code changes. Minutes.
- **Intermediate** — needs an external part (LED, button, sensor, USB–TTL
  adapter), a second board, a small code tweak + reflash, or deliberately
  triggering a fault. A bench task.
- **Advanced** — needs instruments (oscilloscope / logic analyzer / bench PSU),
  a long soak run, multi-chip portability builds, or standing up the Unity
  on-target test-runner. Specialized effort.

**Priority** — importance / risk, independent of complexity.
- **P1** — correctness-critical; needed to trust the module. Do first.
- **P2** — important coverage; variants and secondary paths.
- **P3** — robustness / polish; stress, portability, accuracy margins.

**Status** — `Pending` / `Passed` / `Failed` / `Blocked`. Update in place.

**ID scheme** — `B-nn` basic, `I-nn` intermediate, `A-nn` advanced. IDs are
stable; when inserting a higher-priority test, give it a new trailing number and
re-order the list (don't renumber existing IDs).

**Convention when appending** — add the test to the correct complexity group,
placed by priority, with: Module, Priority, Status, Setup, Steps, Expected.

---

## Basic (flash + observe serial)

### B-01 — NVS persists across a full power cycle · P1 · Module: e32p_nvs · Status: Pending
Confirms data survives loss of power, not just a warm reset.
- **Setup:** `02_nvs_boot_count` flashed.
- **Steps:**
  1. Open the monitor, note `boot count = N`.
  2. Fully remove USB power (not EN) for ~5 s, reconnect, reopen the monitor.
  3. Repeat twice.
- **Expected:** the count increments by exactly 1 each power cycle (N → N+1 →
  N+2); no `e32p_nvs` error line on any boot.

### B-02 — GPIO input read reflects a driven level · P1 · Module: e32p_gpio · Status: Pending
Exercises the input path (only output was shown so far), no wiring using two pins.
- **Setup:** small tweak to `00_blink_gpio`: configure GPIO4 as output, GPIO5 as
  input with pull-down; jumper GPIO4→GPIO5; log `get_level(5)` each loop.
- **Steps:** drive GPIO4 high/low each second; read GPIO5.
- **Expected:** GPIO5 reads 1 when GPIO4 is high, 0 when low; matches every cycle.

### B-03 — UART read timeout returns partial/zero without blocking forever · P1 · Module: e32p_uart · Status: Pending
- **Setup:** tweak `04_uart_loopback`: disable loopback, then attempt a read with
  a 200 ms timeout and nothing connected.
- **Steps:** call `e32p_uart_read(h, buf, 8, 200)` in a loop; log the return.
- **Expected:** returns `0` (or < requested) after ~200 ms each time; task keeps
  running; never hangs.

### B-04 — sysinfo MAC and flash size match the tooling · P2 · Module: e32p_sysinfo · Status: Pending
- **Setup:** `03_sysinfo_wdt` flashed; note the MAC esptool prints during flash.
- **Steps:** compare the `e32p_sysinfo: MAC=...` line and `flash=4MB` to esptool's
  `MAC:` line and the board's actual flash.
- **Expected:** MAC identical; flash size correct.

### B-05 — Event handler unregister stops delivery · P2 · Module: e32p_event · Status: Pending
- **Setup:** tweak a demo: register a handler, post → observe; unregister, post
  again.
- **Steps:** post before and after `e32p_event_handler_unregister`.
- **Expected:** the handler logs on the first post and is silent after unregister.

### B-07 — ADC demo boots and reads plausibly while floating · P2 · Module: e32p_adc · Status: Passed (2026-08-10)
Minimal no-hardware check for `07_adc_read` (deferred — compile-proven but not yet
flashed).
- **Setup:** flash `07_adc_read`.
- **Steps:** watch a few log lines with GPIO34 left floating.
- **Expected:** boots cleanly; `raw` is a plausible 0..4095 value (noisy/mid), and
  a calibrated `mV` is printed (proves eFuse Vref calibration is active). The
  full swing test is I-14.

### B-08 — gptimer alarm fires at the expected rate · P2 · Module: e32p_gptimer · Status: Passed (2026-08-10)
- **Setup:** flash `08_gptimer_alarm`.
- **Steps:** watch the log for ~5 s.
- **Expected:** `alarms` increments exactly once per ~1 s of `raw` microseconds
  (e.g. raw≈1000000 → alarms=1, raw≈2000000 → alarms=2); `raw` advances
  continuously. Confirms the hardware alarm ISR fires on schedule.

### B-09 — DAC demo boots; both modules init and sweep · P2 · Module: e32p_dac + e32p_adc · Status: Passed (2026-08-10, init/run)
**Finding:** the original idea (on-chip DAC→ADC2 readback, no parts) does NOT
work — GPIO25's DAC output and ADC2 input contend for the same RTC pad through
the oneshot drivers, so ADC2 reads a floating ~200 mV floor with no correlation
to the DAC code (verified on device, both pad orderings). The demo now reads back
on ADC1/GPIO34 with a jumper (see I-16).
- **Setup:** flash `09_dac_adc_loopback` (no jumper).
- **Steps:** watch a sweep.
- **Expected:** `dac ch0 init` + `adc1 unit init` both log; the DAC code steps
  0→255 each sweep. Metering GPIO25 shows it stepping 0→~3.3 V (the full output
  verification is I-16).

### B-10 — Touch value responds to a finger · P1 · Module: e32p_touch · Status: Passed (2026-08-10)
- **Setup:** flash `10_touch_read`.
- **Steps:** note the resting `touch=` value, then touch/hover over the GPIO4 pin.
- **Expected:** the value drops well below the baseline and `[TOUCHED]` appears;
  it returns to baseline when released. (Values drift with board/humidity, hence
  the startup baseline.)

### B-11 — PCNT counts internally-generated pulses · P1 · Module: e32p_pcnt · Status: Passed (2026-08-11)
- **Setup:** flash `11_pcnt_count`.
- **Steps:** watch a few cycles.
- **Expected:** every line reads `generated 50 pulses -> pcnt count = 50 [PASS]`
  — the count exactly equals the pulses (io_loop_back, no wiring).

### B-12 — ESP-NOW broadcast TX succeeds (one board) · P2 · Module: e32p_espnow · Status: Passed (2026-08-11)
Note: the first run also auto-connected to a stored AP (a coupling bug — e32p_wifi
auto-associated on STA_START regardless of intent). Fixed: e32p_wifi now only
auto-connects when e32p_wifi_sta_connect asked for it. Re-flash to see ESP-NOW no
longer join an AP.
- **Setup:** flash `13_espnow_broadcast`.
- **Steps:** watch a few cycles.
- **Expected:** `esp-now ready`, the board MAC prints, then each line reads
  `broadcast "e32p hello #N" [sent]`. (No RX with one board — ESP-NOW doesn't
  self-receive.)

### B-13 — TWAI installs and transmits (bare board) · P2 · Module: e32p_twai · Status: Passed (2026-08-11)
Observed: `twai installed`, first ~6 frames `tx=ESP_OK` (controller works), then
`tx=ESP_ERR_TIMEOUT` as the controller goes error-passive with no bus/loopback —
expected on a disconnected bus. Full frame loopback is I-20 (needs a jumper).
- **Setup:** flash `14_twai_loopback`.
- **Steps:** watch a few cycles.
- **Expected:** `twai installed ...`, then `tx=ESP_OK rx=ESP_ERR_TIMEOUT` each
  cycle — the frame queues/sends; RX times out because nothing loops it back
  (TWAI has no internal loopback). Full loopback is I-20.

### B-14 — I2S TX streams a tone (bare board) · P2 · Module: e32p_i2s · Status: Passed (2026-08-11)
- **Setup:** flash `15_i2s_tone`.
- **Steps:** watch a few cycles.
- **Expected:** `i2s0 TX init ...`, then steady `streaming 440 Hz tone, ~31 KB/s`
  with no errors (16000 samples/s × 2 bytes). Proves the I2S engine clocks data
  out; audio is I-21.

### B-15 — WS2812 refresh clocks out frames (bare board) · P2 · Module: e32p_ws2812 · Status: Passed (2026-08-11)
- **Setup:** flash `16_ws2812_rainbow` (nothing connected to GPIO18).
- **Steps:** watch a few log lines.
- **Expected:** `ws2812 init: gpio=18 leds=8`, then periodic `frame N sent (hue
  base ...)` with no errors. Proves the RMT TX + composite-encoder path clocks a
  full GRB frame + latch out each cycle; visible color is I-22.

### B-16 — J1939 codec + transmit path (bare board) · P1 · Module: e32p_j1939 · Status: Passed (2026-08-12)
- **Setup:** flash `17_j1939_send` (TWAI NO_ACK self-test; nothing on GPIO21/22).
- **Steps:** watch the startup burst.
- **Observed (2026-08-12):** `j1939 node init: SA=0x80 prio=6`, then a burst of
  `tx PGN=65262 SA=0x80 DA=0xFF id=0x18FEEE80 data[0]=N ok` (N=0..5). The
  composed 29-bit ID **`0x18FEEE80`** is exactly the correct J1939 encoding —
  the codec proof. TX then stalls (`TX stalled after N frames, ESP_ERR_TIMEOUT`)
  and the demo idles: **expected without a bus** — NO_ACK self-test bit-monitors
  RX, sees no loopback, auto-retransmits, and the TX queue fills. The pure ID
  codec (PDU1/PDU2, PF=240 boundary, known real IDs) is also covered headless by
  A-01's `e32p_j1939` suite (7 cases, all pass).
- **With a GPIO21→22 jumper / transceiver:** TX drains and the `rx PGN=65262
  prio=6 SA=0x80 DA=0xFF len=8 data[0]=N` line appears, proving encode→transmit→
  receive→decode end-to-end (local loopback; a real external peer is I-23).

### B-17 — J1939 TP BAM transfer end-to-end (bare board) · P1 · Module: e32p_j1939tp · Status: Passed (2026-08-12)
- **Observed (2026-08-12):** 19 consecutive `BAM transfer #N: OK (250 bytes,
  bytes match)` with no timeouts — a 36-packet BAM segment→reassemble each round,
  fully on-device. (An earlier demo revision timed out after packet 1 on repeat
  transfers: the loop stamped a session's timer with a stale cached poll() time;
  fixed by polling both engines before pumping frames — the engine was never at
  fault, A-01's TP suite always passed.)
- **Setup:** flash `18_j1939tp_bam` (two TP engines in one app, in-process
  loopback; nothing connected).
- **Steps:** watch a few rounds.
- **Expected:** `two TP engines (SA 0x80 -> 0x21)`, then each round
  `receiver got PGN=65259 from SA=0x80, 250 bytes` and `BAM transfer #N: OK
  (250 bytes, bytes match)`. Proves the full BAM segment→reassemble path
  (36 packets) on-device with no bus. The RTS/CTS multi-burst, T1-timeout abort,
  and byte-identical reassembly are covered headless by A-01's `e32p_j1939tp`
  suite (two engines + virtual clock).

### B-18 — J1939 address-claim contention (bare board) · P1 · Module: e32p_j1939_acl · Status: Passed (2026-08-14)
- **Setup:** flash `19_j1939_acl` (two ACL engines in one app, in-process; nothing
  connected).
- **Steps:** watch a few rounds.
- **Expected:** each round `A claimed=0x80 (ok), B claimed=0x81 (ok)` — the lower
  NAME (A) wins 0x80, the higher NAME (B, arbitrary-address-capable) relocates to
  a free self-configurable address. Proves the claim/arbitration/relocate path
  on-device. NAME codec, cannot-claim, and Request→reply are covered headless by
  A-01's `e32p_j1939_acl` suite.

### I-25 — J1939 address claiming over a real bus · P1 · Module: e32p_j1939_acl · Status: Pending
- **Setup:** two nodes (transceivers, 250 kbit/s) both preferring the same SA, or
  a J1939 bus tool that claims/requests addresses.
- **Steps:** start both; issue a Request for Address Claimed from the tool.
- **Expected:** the lower-NAME node keeps the address; the higher-NAME node
  relocates or reports Cannot Claim; both answer the Request. Confirms claiming
  against an independent J1939 implementation on a real wire.

### B-19 — BMP280 compensation on-target (bare board) · P2 · Module: e32p_bmp280 · Status: Passed (2026-08-14)
- **Setup:** flash `20_bmp280` with no sensor connected.
- **Steps:** watch the boot log.
- **Expected:** `reference vector (no sensor): T=25.08 C  P=100653 Pa` — the pure
  Bosch compensation runs on-device and matches the datasheet reference vector,
  then a `no BMP280 found` warning (init returns NOT_FOUND without the part).
  Parse/compensate/div-by-zero are also covered headless by A-01's `e32p_bmp280`
  suite.

### I-26 — BMP280 live read over I2C · P2 · Module: e32p_bmp280 · Status: Pending (needs BMP280)
- **Setup:** BMP280 at 0x76 on SDA=21/SCL=22, 3V3 (the ordered BMP280-3.3). Flash
  `20_bmp280`, or run the HIL `sensor_bmp280` test (`pytest --port COM5
  --with-sensors test_sensor.py`).
- **Steps:** power on; read the once-a-second samples.
- **Expected:** plausible room temperature and ~950–1030 hPa pressure. The HIL
  test additionally range-checks the reading before reporting PASS.

### I-27 — TM1637 display · P2 · Module: e32p_tm1637 · Status: Pending (needs display)
- **Setup:** TM1637 4-digit module on CLK=18/DIO=19, 3V3. Flash `21_tm1637`, or run
  the HIL `sensor_tm1637` test (`--with-sensors`).
- **Steps:** watch the display through the demo.
- **Expected:** shows `1234`, sweeps brightness on `8888`, then a ticking `mm:ss`
  clock with the colon lit. Visual confirmation (the bus is write-only). Font and
  formatting are covered headless by A-01's `e32p_tm1637` suite.

### B-21 — MQTT self-test on-target (bare board) · P2 · Module: e32p_mqtt · Status: Pending (no parts needed)
- **Setup:** covered by A-01's `e32p_mqtt` suite and the HIL `mqtt_selftest`
  (`pytest --port COM5 test_mqtt.py`) — no network needed.
- **Steps:** run the unit suite / HIL self-test.
- **Expected:** all topic-match cases pass (`+`/`#` wildcards, `#` matches parent,
  a wildcard filter rejects `$` topics), event strings map, and the esp-mqtt
  client init/deinit works on-target without connecting.

### I-28 — MQTT broker round-trip · P2 · Module: e32p_mqtt · Status: Passed (2026-08-18, Mosquitto on LAN) · Automated: tests/hil/host/test_net_mqtt.py
- **Setup:** set Wi-Fi SSID/password + broker via `idf.py menuconfig` -> "23_mqtt
  example", flash `23_mqtt`. Have an MQTT client to poke it (e.g. `mosquitto_pub`
  / `mosquitto_sub` against the same broker).
- **Steps:** watch the log connect and heartbeat; subscribe to `e32p/demo/status`
  from the other client; publish to `e32p/demo/cmd`.
- **Expected:** device logs `broker connected`, publishes retained `online` then
  `heartbeat N` every 5 s (visible to the subscriber), and prints any message
  sent to `e32p/demo/cmd` as `RX e32p/demo/cmd = <payload>`. Confirms connect,
  subscribe, publish, and inbound delivery against a real broker.

### B-22 — Modbus protocol self-test on-target (bare board) · P2 · Module: e32p_modbus · Status: Passed (2026-08-18, via A-01)
- **Setup:** covered by A-01's `e32p_modbus` suite and the HIL `modbus_selftest`
  (`pytest --port COM5 test_modbus.py`) — no wire.
- **Steps:** run the unit suite / HIL self-test.
- **Expected:** CRC-16 matches the catalogue (`0x4B37`), request/response frames
  match reference bytes, a full master→slave→master register round-trip returns
  the written values, and out-of-range/illegal requests produce the right
  exceptions.

### I-29 — Modbus RTU master↔slave over UART · P2 · Module: e32p_modbus · Status: Pending (needs 2 jumpers; no-wire timeout path confirmed 2026-08-18)
- **Setup:** flash `24_modbus`. Cross-wire the two UARTs on the board: UART1
  TX(GPIO17)→UART2 RX(GPIO5), UART2 TX(GPIO4)→UART1 RX(GPIO16).
- **Steps:** watch the serial log.
- **Expected:** each cycle logs `holding[0..3] = <n> 200 300 400` with the first
  value climbing (1000, 1001, …) — the master writes it via 0x06 and reads it
  back via 0x03 over real serial lines to the in-firmware slave. Without the
  jumpers the master logs `write failed: ESP_ERR_TIMEOUT`. Proves the full UART
  transport + framing end to end (no RS-485 hardware needed).

### B-23 — OTA query self-test on-target (bare board) · P2 · Module: e32p_ota · Status: Pending (no parts needed)
- **Setup:** covered by A-01's `e32p_ota` suite and the HIL `ota_query`
  (`pytest --port COM5 test_ota.py`) — no server.
- **Steps:** run the unit suite / HIL self-test.
- **Expected:** running slot and next slot are distinct OTA partitions, the app
  description is populated, the running image size is plausible (>0, < slot size),
  and the streaming/URL entry points reject bad args.

### B-24 — Self-OTA slot hop + rollback confirm (bare board) · P2 · Module: e32p_ota · Status: Pending (no parts needed)
- **Setup:** flash `25_ota` (needs no server or wiring). It clones its own image
  into the other slot and reboots, up to twice.
- **Steps:** watch the serial log across the reboots.
- **Expected:** boot 1 runs from `ota_0`, copies itself into `ota_1` (progress
  0→100%), and reboots; boot 2 runs from `ota_1` in **pending-verify**, logs
  `marking valid`, copies back into `ota_0`, reboots; boot 3 runs from `ota_0`,
  marks valid, and idles (`demo complete after 2 hop(s)`). Proves esp_ota
  begin/write/end + set-boot + rollback confirmation end to end, no network. A
  failed clone logs `self-OTA failed … boot slot unchanged` (can't brick).

### B-25 — BLE advertising payload on-target (bare board) · P2 · Module: e32p_ble · Status: Pending (no parts needed)
- **Setup:** covered by A-01's `e32p_ble` suite and the HIL `ble_selftest`
  (`pytest --port COM5 test_ble.py`) — no radio.
- **Steps:** run the unit suite / HIL self-test.
- **Expected:** UUID helpers and the advertising-payload assembly match the
  expected bytes (`02 01 06 03 03 FF 00 05 09 65 33 32 70` for flags + service
  0x00FF + name "e32p"), oversized payloads are rejected, and the peripheral
  functions return `ESP_ERR_NOT_SUPPORTED` when the BLE host isn't built in.

### I-31 — BLE peripheral connect + notify · P2 · Module: e32p_ble · Status: Passed (2026-08-19; phone paired Just-Works, stable connect + 60 s notify stream)
- **Setup:** flash `27_ble` (enables NimBLE). On a phone, install nRF Connect or
  LightBlue.
- **Steps:** scan for `e32p-ble`, connect, discover service 0x00FF; subscribe to
  characteristic 0x2A56; write text to 0x2A57.
- **Expected:** notifications on 0x2A56 arrive as a ticking counter, the write to
  0x2A57 prints `write to char 1: "..."` in the serial log, and connect/disconnect
  log lines fire. Confirms advertising, GATT, notify, and the write callback on a
  real BLE link. Advertising/UUID assembly is already covered by A-01 / B-25.

### I-32 — Edge gateway end-to-end (flagship) · P2 · App: 29_edge_gateway · Status: Passed (2026-08-24, sim source → LAN Mosquitto @192.168.0.41; telemetry JSON @5s then 2s, interval + identify commands, flat heap ~160KB, no WDT reset over 88s). Real J1939 source still pending SN65HVD230.
- **Setup:** an MQTT broker on the LAN (e.g. Mosquitto). Fast path: set the
  fallback Wi-Fi SSID/password/broker in `menuconfig -> 29_edge_gateway`, flash,
  and the gateway uses the built-in `sim` field source. Full path: leave the
  fallbacks empty and commission over BLE first (see steps).
- **Steps (cloud pipeline):** watch the serial log for `Wi-Fi up` and `online`;
  subscribe from a PC (`mosquitto_sub -t 'gw/#' -v`) to see telemetry; publish
  commands (`mosquitto_pub -t gw/gw-01/cmd -m "interval 2"`, `... -m identify`).
- **Steps (provisioning):** with no config stored, connect to `e32p-gateway`
  over BLE, write ssid/pass/broker/id to 0xFF01..0xFF04, then write 0xFF05 to
  apply — the device saves to NVS and reboots into normal operation.
- **Expected:** JSON telemetry on `gw/<id>/telemetry` every *interval* seconds
  with the four sim signals + health (heap/rssi/uptime); `interval` changes the
  rate and persists; `identify` blinks the LED; `reboot` restarts the device.
  Exercises Wi-Fi, MQTT, BLE, NVS, WDT, sysinfo and the display together in one
  app.
- **Note:** the real J1939 source (`menuconfig -> Read a real J1939/CAN bus`)
  needs an SN65HVD230-class transceiver — verify once the hardware arrives.

### B-26 — Signed OTA rejects bad images (bare board) · P2 · Module: e32p_ota + Secure OTA · Status: Passed (2026-08-19; integrity via checksum + authenticity via "image valid, signature bad")
- **Setup:** flash `28_secure_ota` (built with `CONFIG_SECURE_SIGNED_ON_UPDATE_NO_SECURE_BOOT`
  + an ECDSA signing key; no eFuses burned, fully reversible). No server or wiring.
- **Steps:** watch the serial log; two self-contained tests run.
- **Expected:**
  - **[1/2] integrity** — a clone with one flipped app byte fails the image
    checksum → `integrity: REJECTED` (verified 2026-08-19: `Checksum failed`).
  - **[2/2] authenticity** — a clone with the appended **signature block omitted**
    has valid contents but no valid signature → `authenticity: REJECTED` via the
    signature check (an `esp_image` signature/verification error, not a checksum
    one). This isolates "signed by our key" from "not corrupted".
  - Either way the boot slot is unchanged (a safety net reverts it if a test
    unexpectedly passes), so the device stays on trusted firmware.
  See docs/SECURITY_HARDENING.md for the eFuse-based Secure Boot v2 + Flash
  Encryption (documented, not run on the dev board).

### B-27 — ISO-TP segmented echo, loopback (bare board) · P1 · Module: e32p_isotp · Status: Passed (2026-09-11)
- **Observed (2026-09-11):** `PASS #n len=100 t=29ms` every round — 100 bytes segmented,
  reassembled and echoed back byte for byte, with nothing wired.
- **Setup:** flash `examples/31_isotp_echo` with its default role (Loopback).
- **Steps:** watch a few rounds.
- **Expected:** `LOOPBACK: two engines, in-process queue, 100-byte payload`, then one
  `PASS` line every 2 s. The protocol's corner cases — flow control both ways, timers,
  sequence errors, BS=0, STmin, concurrent TX/RX, back-pressure — are covered with no
  board by A-01's `e32p_isotp` suite.

### B-20 — Low-power sleep cycle (bare board) · P2 · Module: e32p_pm · Status: Passed (2026-08-18)
- **Setup:** flash `22_pm` with nothing connected.
- **Steps:** watch the serial log across a few wake cycles.
- **Expected:** each cycle logs `boot #N — woke by: <cause>`, then `light-sleeping
  2 s` → `back from light sleep — woke by: timer`, then `deep-sleeping 5 s`. After
  the deep-sleep reset, `boot #N` **increments** (RTC memory retained) and the
  wake cause becomes `timer`. Proves light-sleep return, deep-sleep reset, wake
  cause, and RTC retention on-device. Wake-cause mapping, RTC-pin/arg validation,
  and a live light-sleep round-trip are also covered by A-01's `e32p_pm` suite and
  the HIL `pm_lightsleep` / `pm_wakecause` tests (single board, run now).

### I-24 — J1939 TP transfer over a real bus · P1 · Module: e32p_j1939tp · Status: Pending
- **Setup:** a CAN transceiver on GPIO21/22 (250 kbit/s, 120 Ω) and a J1939 peer
  or bus tool that supports TP. Wire the TP `tx` sink to `e32p_j1939_send` and
  feed `e32p_j1939_receive` frames to `e32p_j1939tp_on_frame` (per the README).
- **Steps:** send a >8-byte PGN from the ESP32 (BAM and destination-specific);
  from the tool, send a multi-packet PGN back.
- **Expected:** the tool reassembles the ESP32's BAM/RTS-CTS transfer; the ESP32
  delivers the tool's multi-packet message via `on_rx` byte-identical. Confirms
  the transport against an independent J1939 stack on a real wire.

### B-06 — NVS string round-trip on hardware · P2 · Module: e32p_nvs · Status: Pending
- **Setup:** tweak a demo to `write_str`/`read_str` a value across a reset.
- **Steps:** write `"hello"` to `storage/name`, reset, read it back.
- **Expected:** reads back `"hello"`; length-query form (`out=NULL`) reports the
  right size.

---

## Intermediate (external part / fault injection / code tweak)

### I-01 — I2C detects a real device · P1 · Module: e32p_i2c · Status: Pending
The gap the bare-board scan can't cover (your example).
- **Setup:** any I2C sensor (e.g. BMP280/BME280 @ 0x76/0x77, or an SSD1306 @
  0x3C). Wire SDA→GPIO21, SCL→GPIO22, VCC→3V3, GND→GND; add 4.7 kΩ pull-ups if the
  breakout lacks them. Flash `05_i2c_scan`.
- **Steps:** power up, watch the scan.
- **Expected:** `found 1 device(s): 0x76` (or the device's address); removing the
  device returns to `no I2C devices found` within one scan cycle.

### I-02 — I2C register read returns the right chip ID · P1 · Module: e32p_i2c · Status: Pending
Proves `add_device` + `read_reg`, not just probe.
- **Setup:** BMP280 wired as in I-01. Tweak a demo: `add_device(bus, 0x76,
  400000, &dev)`, `read_reg(dev, 0xD0, &id, 1, 100)`.
- **Steps:** read the ID register.
- **Expected:** `id == 0x58` (BMP280) or `0x60` (BME280); repeated reads stable.

### I-03 — Task watchdog actually resets a hung task · P1 · Module: e32p_wdt · Status: Pending
- **Setup:** `03_sysinfo_wdt`; set `FEED_THE_DOG` to `0`; reflash.
- **Steps:** let it run ~4 s.
- **Expected:** after ~3 s the board panics + reboots; the **next** boot logs
  `reset reason: task watchdog`. Restore `FEED_THE_DOG=1` afterward.

### I-04 — GPIO interrupt fires on an edge · P1 · Module: e32p_gpio · Status: Pending
Covers `install_isr_service` / `add_isr_handler` / `intr_enable`, untested so far.
- **Setup:** button (or jumper) from GPIO0-safe pin (e.g. GPIO18) to GND, input
  with pull-up, `E32P_GPIO_INTR_FALLING`. ISR sets a flag; main logs it.
- **Steps:** press the button several times.
- **Expected:** each press logs exactly one edge event; no missed/duplicate under
  normal presses (debounce noise acceptable).

### I-05 — sysinfo reset-reason transitions · P2 · Module: e32p_sysinfo · Status: Pending
- **Setup:** `03_sysinfo_wdt`.
- **Steps:** observe reset reason after (a) power-on, (b) EN button, (c) a
  software `esp_restart()` added to the demo, (d) the WDT reset from I-03.
- **Expected:** reasons map correctly: power-on, `software`/external, software,
  `task watchdog` respectively.

### I-12 — SPI loopback round-trip with a MOSI→MISO jumper · P1 · Module: e32p_spi · Status: Pending
- **Setup:** `06_spi_loopback`; jumper wire from GPIO23 (MOSI) to GPIO19 (MISO).
- **Steps:** flash, open monitor; observe the match field; remove the jumper and
  observe again.
- **Expected:** with the jumper, `rx` equals `tx` every line → `PASS (loopback)`;
  without it, `transfer ok` still prints but `rx` is undefined → `no jumper`.

### I-13 — SPI talks to a real device · P1 · Module: e32p_spi · Status: Pending
- **Setup:** a SPI device (e.g. an SPI NOR flash or an ADXL345). Wire SCLK=18,
  MOSI=23, MISO=19, CS=21, plus power/GND. Tweak the demo: `add_device` with the
  device's mode/clock, then read a known ID register (e.g. JEDEC `0x9F`).
- **Steps:** issue the ID read transaction.
- **Expected:** the returned bytes match the device's datasheet ID; repeated
  reads are stable.

### I-14 — ADC reading tracks a known input · P1 · Module: e32p_adc · Status: Pending
- **Setup:** `07_adc_read`; a jumper wire (and optionally a potentiometer).
- **Steps:** jumper GPIO34 → 3V3, then → GND, then sweep a pot.
- **Expected:** 3V3 → `raw` ≈ 4095, `mV` ≈ 3100; GND → `raw` ≈ 0, `mV` ≈ 0; pot
  sweeps smoothly between. Calibrated `mV` present (this chip has eFuse Vref).

### I-16 — DAC output tracks on ADC1 via a GPIO25→GPIO34 jumper · P1 · Module: e32p_dac + e32p_adc · Status: Pending
- **Setup:** `09_dac_adc_loopback`; one jumper wire GPIO25 → GPIO34.
- **Steps:** flash, wire the jumper, watch a sweep.
- **Expected:** ADC `raw`/`mv` climbs monotonically with the DAC code and roughly
  matches in mV (dac=128 ~1650 mV → adc ~1600 mV; dac=255 → ~4095). Without the
  jumper GPIO34 floats and the ADC reads ~0. (ADC1 is used because ADC2/GPIO25
  shares the DAC pad — see B-09.)

### I-17 — Wi-Fi Station connects and gets an IP · P1 · Module: e32p_wifi · Status: Passed (2026-08-11, IP 192.168.0.29, RSSI -47 dBm)
- **Setup:** `12_wifi_sta`; set SSID/password via `idf.py menuconfig` (Example
  Wi-Fi Configuration); a reachable 2.4 GHz AP.
- **Steps:** flash and watch the log.
- **Expected:** `connected, IP=<addr>  RSSI=<dBm>`; the IP is on your subnet and
  pingable. Wrong password → `connect failed: ESP_FAIL`; no AP → `ESP_ERR_TIMEOUT`.

### I-19 — Two-board ESP-NOW exchange · P1 · Module: e32p_espnow · Status: Pending
- **Setup:** two ESP32 boards, both flashed with `13_espnow_broadcast`.
- **Steps:** power both, open a monitor on each (or one, note the other's MAC).
- **Expected:** each board logs `RX from <other-MAC> : "e32p hello #N"` — real
  board-to-board messaging with no AP.

### I-18 — Wi-Fi SoftAP accepts a client · P2 · Module: e32p_wifi · Status: Pending
- **Setup:** a tweak calling `e32p_wifi_ap_start("e32p-demo", "12345678", 1, 4)`.
- **Steps:** connect a phone/laptop to the `e32p-demo` network.
- **Expected:** the client associates and gets a DHCP lease from the ESP32.

### I-20 — TWAI frame loopback via a TX→RX jumper (or transceiver) · P1 · Module: e32p_twai · Status: Pending
- **Setup:** `14_twai_loopback`; a jumper wire GPIO21 → GPIO22 (or a CAN
  transceiver in loopback).
- **Steps:** flash, wire the jumper, watch a sweep.
- **Expected:** `sent id=0x123 -> got id=0x123 data=dead..ef [PASS]` — the self
  frame returns byte-identical. Without the jumper, `rx=ESP_ERR_TIMEOUT`.

### I-21 — I2S plays an audible tone via a MAX98357A · P2 · Module: e32p_i2s · Status: Pending
- **Setup:** `15_i2s_tone`; a MAX98357A I2S amp + small speaker. Wire BCLK=26,
  LRC=25, DIN=22, VIN=3V3, GND.
- **Steps:** flash and listen.
- **Expected:** a clean, steady 440 Hz tone (concert A) from the speaker.

### I-22 — WS2812 rainbow animates on a real strip · P2 · Module: e32p_ws2812 · Status: Pending
- **Setup:** an 8-pixel (or longer) WS2812/NeoPixel strip. Wire DIN=GPIO18,
  +5V=VIN/5V, GND=GND (common ground). For long/bright strips use a separate
  5 V supply and, ideally, a 3.3→5 V level shifter on DIN.
- **Steps:** flash `16_ws2812_rainbow`; watch the strip. If `NUM_LEDS` differs
  from your strip, adjust it in `main.c`.
- **Expected:** a smooth rainbow that scrolls along the strip, ~20 fps, no
  flicker. Confirms correct GRB order, bit timing, and the reset/latch pulse.

### I-23 — J1939 message received from a real bus peer · P1 · Module: e32p_j1939 · Status: Pending
- **Setup:** a CAN transceiver (e.g. SN65HVD230) on GPIO21/22 with 120 Ω
  termination, and a J1939 peer or bus tool (PCAN/USB-CAN with a J1939 decoder)
  at 250 kbit/s. Set `17_j1939_send` to `E32P_TWAI_MODE_NORMAL`,
  `self_reception=false`.
- **Steps:** run both nodes; on the tool, decode the broadcast; from the tool,
  send a PGN back to SA 0x80.
- **Expected:** the tool decodes PGN 65262 from SA 0x80 with the rolling counter;
  the ESP32 logs frames the tool sends, PGN/SA/DA decoded correctly. Confirms
  the codec against an independent J1939 implementation on a real wire.

### I-33 — ISO-TP over a real CAN bus, two boards · P1 · Module: e32p_isotp · Status: Passed (2026-09-15)
- **Observed:** example 31 node A (WROOM) ↔ node B (S3) at 500 kbit: 12/12
  `PASS len=100` at 24–25 ms (2026-09-11), with a CANable capture decoded frame by frame
  in `examples/31_isotp_echo/README.md`. HIL `test_isotp.py`: all 7 at 250 kbit, twice
  back to back (2026-09-15, `tests/hil/BASELINE.md`).
- **Found on the way:** an engine bug that failed every multi-frame transfer with
  `ESP_ERR_TIMEOUT` when a channel was used long after boot. Fixed, with a Unity case.
- **Setup:** CAN wiring per `tests/hil/WIRING.md` §7.1; CANable unplugged, or its port
  closed (§12).
- **Steps:** `pytest test_isotp.py --port COM5 --companion-port COM7`.
- **Expected:** 7 passed. Every verdict carries `rate=` and `addr=`, checked against
  `--can-bitrate` / `--isotp-addr`.

### I-34 — OBD-II over a real CAN bus, two boards · P1 · Module: e32p_obd2 · Status: Passed (11-bit 2026-10-02, 29-bit 2026-10-05)
- **What it proves:** the J1979 server answers a real tester across a real bus, including
  the two cases a bench loopback cannot show — a broadcast answered from the ECU's own
  response ID, and an unsupported PID that must draw *silence* on the broadcast address.
- **Setup:** CAN wiring per `tests/hil/WIRING.md` §7.1, companion flashed with the same
  build as the DUT; CANable unplugged or its port closed (§12).
- **Steps:** `pytest test_obd2.py --port COM5 --companion-port COM7`.
- **Expected:** 6 passed. The DTC cases seed themselves first (`RUN obd2_dtc_seed` to the
  companion), so they run in any order and repeat cleanly; OBD-II has no service that
  stores a code.
- **Observed:** 6/6 on the first run at 250 kbit 11-bit, and 6/6 again in the 29-bit
  normal-fixed sweep (2026-10-05, `tests/hil/BASELINE.md`).

### I-35 — UDS over a real CAN bus, two boards · P1 · Module: e32p_uds · Status: Passed (11-bit 2026-10-06, 29-bit 2026-10-07)
- **What it proves:** the ISO 14229-1 server holds its timing and rules against a real
  tester across a real bus — `S3` and tester present, broadcast answers and broadcast
  silence, security access and its lockout, DID write preconditions, one trouble-code store
  seen through both UDS and OBD-II, the `0x78` "please wait" within `P2`, a 4 KB download
  in maximum-size blocks, and an ECU reset that really restarts the companion.
- **Setup:** CAN wiring per `tests/hil/WIRING.md` §7.1, companion flashed with the same
  build as the DUT; CANable unplugged or its port closed (§12).
- **Steps:** `pytest test_uds.py --port COM5 --companion-port COM7`.
- **Expected:** 9 passed, about 32 s — two tests wait out the 5 s session timeout and the
  10 s security delay on purpose. The reset test waits for the companion to report
  `@EVT READY` and `@EVT UDS server ready` before its second half runs.
- **Observed:** 9/9 on the first run; the full suite with it in, 74 passed / 4 skipped,
  twice (2026-10-06). `0x78` 1 ms after the request, the erase result after 2002 ms, the
  download at 3.7 kB/s, the companion back 0.72 s after the reset. 29-bit normal-fixed
  sweep (2026-10-07): the same, inside a full suite of 76 passed / 4 skipped, twice.

### I-06 — LEDC start/stop gates the output; fade is smooth · P2 · Module: e32p_ledc · Status: Pending
- **Setup:** LED + ~330 Ω on GPIO18, `01_pwm_fade`.
- **Steps:** watch the breathe; add a `stop()`/`start()` toggle and observe.
- **Expected:** LED brightness ramps up/down smoothly; `stop()` drives it fully
  off; `start()` resumes at the stored duty.

### I-07 — UART talks to a real peer · P2 · Module: e32p_uart · Status: Pending
- **Setup:** USB–TTL adapter; TX→adapter RX, RX→adapter TX, common GND; terminal
  at matching baud. Disable loopback in the demo.
- **Steps:** send from the board; type into the terminal to the board.
- **Expected:** both directions carry bytes intact at 115200 8N1.

### I-08 — UART honors baud / parity / stop-bit settings · P2 · Module: e32p_uart · Status: Pending
- **Setup:** loopback demo; vary `baud_rate` (9600, 921600), `parity`, `stop_bits`.
- **Steps:** run loopback at each combination.
- **Expected:** loopback still `PASS` for every valid combination.

### I-09 — I2C multiple devices on one bus · P2 · Module: e32p_i2c · Status: Pending
- **Setup:** two I2C devices at different addresses on the same SDA/SCL.
- **Steps:** scan; add both as devices; read each.
- **Expected:** both addresses found; both devices read correctly; no
  cross-talk.

### I-10 — GPIO pin-guard rejects reserved/input-only pins on hardware path · P2 · Module: e32p_gpio · Status: Pending
- **Setup:** tweak demo to call `e32p_gpio_config` on GPIO7 (flash) and GPIO34 as
  output.
- **Expected:** GPIO7 → `ESP_ERR_E32P_PIN_RESERVED` + logged; GPIO34 output →
  `ESP_ERR_INVALID_ARG`; board keeps running (no crash/brownout).

### I-15 — ADC2 read returns RESOURCE_CONFLICT while Wi-Fi is active · P2 · Module: e32p_adc · Status: Pending (blocked on M5)
- **Setup:** (needs `e32p_wifi`, M5) init ADC2 on a channel, start Wi-Fi, then read.
- **Steps:** read an ADC2 channel before and after Wi-Fi start.
- **Expected:** reads fine before; after Wi-Fi is up, `e32p_adc_read_raw` returns
  `ESP_ERR_E32P_RESOURCE_CONFLICT`. Confirms the documented ADC2/Wi-Fi mapping.

### I-11 — WDT warn-only mode (panic=false) · P3 · Module: e32p_wdt · Status: Pending
- **Setup:** `e32p_wdt_init(3000, false)`, stop feeding.
- **Expected:** the TWDT logs a warning/backtrace repeatedly but the device does
  **not** reboot.

---

## Advanced (instruments / soak / portability / test harness)

### I-30 — OTA pull from an HTTP server · P3 · Module: e32p_ota · Status: Passed (2026-08-19, python http.server on LAN; ota_0 -> ota_1 + mark_valid) · Automated: tests/hil/host/test_net_ota.py
- **Setup:** flash `26_ota_http` (Wi-Fi SSID/password + OTA URL via `idf.py
  menuconfig` -> "26_ota_http example"). On the PC, in the app's `build/` dir, run
  `python -m http.server 8080`; point the URL at `http://<pc-ip>:8080/ota_http.bin`.
- **Steps:** watch the device connect, download, stage, and reboot.
- **Expected:** logs `pulling firmware from …`, downloads via `esp_https_ota`,
  reboots into the other slot in pending-verify, `marking valid`, and idles — the
  running slot has flipped (ota_0 -> ota_1). Confirms the URL path against a real
  server (the offline query/rollback layer is already covered by A-01 / B-23 /
  B-24). Serve a *different* example's .bin to watch that firmware take over.

### A-01 — Run all module unit suites on the Unity runner · P1 · Module: all · Status: Passed (275 tests, 0 failures, 2026-11-03; before that 247 tests, 32 modules incl. e32p_uds, 2026-10-07)
Runner at `tests/unit_runner` (auto-runs via `unity_run_all_tests()`). Latest
run: **275 Tests 0 Failures 0 Ignored** (2026-11-03, after the security remediation
slices A to J, clean build with 0 warnings, flashed to COM5; the host Unity runner gave
UDS 100/0 and OBD-II 27/0 the same day). The run before it:
**247 Tests 0 Failures 0 Ignored** (2026-10-07, 32 modules, including the 84
`e32p_uds` cases (83 shared with the host runner, plus one on-target only that checks
the bus glue's STmin setting) — sessions, S3, the ISO 14229-1 check order, security access with its
lockout, trouble codes, routines, the 0x78 engine and the download services, on a
virtual clock — the 27
`e32p_obd2` cases — one of which writes and re-reads real NVS, then erases its own key — all 33 `e32p_isotp` cases — the 2 newest cover STmin pacing — and the
`e32p_twai` case that keeps the TX pin recessive during install). Adding a suite changes
`TEST_COMPONENTS`, a CMake cache variable, so it needs `fullclean`: after a plain `build`
the new cases simply do not run. Before 2026-09-11 the runner printed no summary line at all:
`unity_run_all_tests()` only loops over the cases, and `UNITY_BEGIN()` / `UNITY_END()`
now wrap it. The run before that, counted by module: **26 modules, 0 Failures** (clean
`Returned from app_main()`, 2026-08-18), including `e32p_j1939_acl`, `e32p_bmp280` (5 cases — calib parse,
temperature + pressure vs. the Bosch reference vector, div-by-zero guard,
bad-args), `e32p_tm1637` (6 cases — font, right-align/pad/colon/sign formatting,
range rejection, bad-args), `e32p_pm` (5 cases — wake-cause mapping, wake-cause
strings, RTC-pin recognition, wake-source arg validation, and a **live
light-sleep round-trip**: arm a 20 ms timer, sleep, confirm it woke on the timer)
and `e32p_mqtt` (6 cases — topic-filter matching with `+`/`#` wildcards, `#`
matches parent, the `$`-topic rule; event strings; and client init / publish /
subscribe argument validation).
The j1939tp suite adds 4 cases (BAM round-trip byte-identical, RTS/CTS multi-burst
with EndOfMsgAck, T1-timeout abort, bad-args) — the transport engine proven
headless via two engines + a virtual clock, no bus.
Earlier fixes that stuck: `e32p_sysinfo: uptime advances` boolean compare (Unity
64-bit asserts are off by default); `e32p_ledc` one-time fade-service install.
**Maintenance:** adding a module's tests requires (1) a `test/CMakeLists.txt`
registering the suite, (2) editing `TEST_COMPONENTS`, (3) `sdkconfig.defaults`,
AND a **`fullclean`** rebuild. `TEST_COMPONENTS` is a `CACHE STRING`: a plain
`clean build` keeps `CMakeCache.txt` and silently reuses the frozen list — this
is how espnow/twai/i2s/ws2812 went missing until 2026-08-11. Only `fullclean`
(or deleting `build/`) re-reads the list. Re-flash after rebuilding.
The runner now exists at `tests/unit_runner` (enables all modules, lists them in
`TEST_COMPONENTS`, auto-runs via `unity_run_all_tests()`). Each module ships
`test/test_*.c`; this executes them on-target.
- **Setup:** flash `tests/unit_runner` (set-target esp32, build, flash, monitor).
- **Steps:** let it auto-run; read the per-case results and the final summary.
- **Expected:** `<n> Tests 0 Failures 0 Ignored`. Any failure names the case and
  file:line — fix, then re-run. Note cases that only pass headless (e.g. SPI
  byte-match needs the I-12 jumper).

### A-02 — LEDC waveform accuracy on a scope/analyzer · P2 · Module: e32p_ledc · Status: Pending
- **Setup:** oscilloscope or logic analyzer on GPIO18.
- **Steps:** set 5 kHz / 13-bit; sweep duty 0/25/50/75/100 %.
- **Expected:** measured frequency within a few % of 5 kHz; duty cycle matches
  setpoints; 0 % = steady low, 100 % = steady high (verify the max-duty edge).

### A-03 — Portability build for S3 / C3 · P2 · Module: all · Status: Pending
Validates the `SOC_*` / `depends on` seam without a rewrite.
- **Setup:** `idf.py set-target esp32s3` (and `esp32c3`) on the examples whose
  peripherals exist on those parts.
- **Steps:** build each; note any module that fails to compile or needs guarding.
- **Expected:** modules build unchanged where the SoC supports the peripheral;
  unsupported ones are hidden by Kconfig `depends on SOC_*`, not build errors.

### A-04 — Heap-stability soak · P2 · Module: all · Status: Pending
- **Setup:** each example (esp. i2c scan, uart loopback) run for several hours
  with `e32p_sysinfo_free_heap()` logged periodically.
- **Steps:** record free heap over time.
- **Expected:** free heap is flat (no downward trend) — no per-iteration leak in
  init/transfer paths.

### A-05 — I2C robustness: stuck bus / missing pull-ups / 400 kHz margin · P3 · Module: e32p_i2c · Status: Pending
- **Setup:** logic analyzer; test with no external pull-ups, then proper ones;
  optionally short SDA low.
- **Expected:** transfers to an absent device return a clean error (not a hang);
  with a stuck SDA the driver reports an error/timeout; 400 kHz edges are clean
  with proper pull-ups.

### A-06 — UART throughput / RX overflow behavior · P3 · Module: e32p_uart · Status: Pending
- **Setup:** high baud (921600+), bursts larger than `rx_buffer_size`, loopback.
- **Expected:** no crash; overflow is reported/handled; `available()` tracks the
  ring correctly; documented behavior matches observation.

### A-07 — Brown-out reset reason · P3 · Module: e32p_sysinfo · Status: Pending
- **Setup:** bench PSU; briefly dip the 3V3 rail below the brown-out threshold.
- **Expected:** next boot reports `reset reason: brownout`. (Handle with care;
  optional.)

### A-08 — SPI clock frequency and mode on a logic analyzer · P3 · Module: e32p_spi · Status: Pending
- **Setup:** logic analyzer on SCLK/MOSI/MISO/CS.
- **Steps:** run `06_spi_loopback`; sweep `clock_hz` (100 kHz, 1 MHz, 10 MHz) and
  `mode` (0..3).
- **Expected:** measured SCLK matches the setpoint within tolerance; CS frames
  each transaction; CPOL/CPHA edges match the selected mode.

---

### A-09 — ISO-TP interop against PC tools (CANable) · P1 · Module: e32p_isotp · Status: Passed (2026-09-15, at 250 k / 500 k / 1 M and in 29-bit normal-fixed mode)
- **What it proves:** the firmware agrees with an ISO-TP implementation we did not write,
  and a third device on the bus confirms every frame independently of both boards.
- **Setup:** CANable on the bus per `tests/hil/WIRING.md` §12; close Cangaroo.
- **Steps:** `pytest test_isotp_canable.py --port COM5 --companion-port COM7 --canable COM8`.
- **Expected:** 2 passed — `test_canable_sniffs_ff_fc_cf` (frame-by-frame check of a DUT ↔
  companion exchange; the adapter runs in normal mode because its slcan firmware passes
  no frames listen-only) and `test_canable_isotp_peer` (the PC as tester via `can-isotp`,
  100 bytes round trip with the companion).

### A-11 — OBD-II interop: the PC as a scan tool (CANable) · P1 · Module: e32p_obd2 · Status: Passed (11-bit 2026-10-02, 29-bit 2026-10-05)
- **What it proves:** the companion's J1979 server answers a scan tool that shares none of
  our code — python-can drives the adapter and `can-isotp` carries the requests, so the
  transport, the addressing and the service bytes are all judged from outside.
- **Setup:** CANable on the bus per `tests/hil/WIRING.md` §12; close Cangaroo.
- **Steps:** `pytest test_obd2_canable.py --port COM5 --companion-port COM7 --canable COM8`.
- **Expected:** 2 passed — `test_obd2_canable_pid_and_vin` (`01 0C` → 1726 rpm, then
  `09 02` → the 20-byte VIN, whose multi-frame reassembly the other implementation does)
  and `test_obd2_canable_broadcast_and_silence` (an unsupported PID answered `7F 01 12`
  physically, `01 0D` → 88 km/h on 0x7DF answered from the ECU's own 0x7E8, and the same
  unsupported PID on 0x7DF drawing nothing for a full second).
- **Note:** in 29-bit mode the tester's two directions are not symmetric — the request
  goes to 0x18DB33F1 and the answer arrives on 0x18DAF110 — so the address is built as
  `Normal_29bits` with explicit ids, not `NormalFixed`.

### A-12 — A real reflash over UDS at 1 Mbit (example 33) · P1 · Module: e32p_uds + e32p_ota · Status: Passed (2026-10-07)
- **What it proves:** an ECU can be reflashed over CAN with UDS: a corrupted image is
  refused and the old one keeps booting; a good image boots, confirms itself and stays.
  Integrity only — images are not signed (security audit item 3).
- **Setup:** example 33 built with `sdkconfig.defaults.ota1m` (image A, `33-0.1.0`,
  flashed over USB) and the same plus `sdkconfig.defaults.version_b` (image B,
  `33-0.2.0`, downloaded); CANable at 1 Mbit. **Every other node must run at 1 Mbit and
  drive its TX pin, or be off the bus** — a board parked in its bootloader corrupts the
  bus (`tests/hil/WIRING.md` §12.5). Secret provisioned with `uds_client.py provision`.
- **Steps:** `uds_client.py flow --canable COM8 --bitrate 1000000 --sink ota --image
  build_ota1m_b/uds_server.bin --corrupt --reset --expect-version 33-0.1.0`, then the same
  without `--corrupt` and with `--expect-version 33-0.2.0`; then reset the board once more.
- **Expected:** corrupted: `37` refused with `0x72`, `FF01` "rejected", `33-0.1.0` after
  the reset. Good: `FF01` "accepted", `33-0.2.0` after the reset from `ota_1`, `new image
  confirmed` on the console; after the extra reset still `ota_1`, no rollback.
- **Observed (2026-10-07):** both runs as expected, 287 blocks in about 77 s each
  (3.7 kB/s) while the ECU asked for STmin 1 ms. With the PC ignoring STmin
  (`--stmin-override 0`, measurement only) the same download took 9.8 s (29.2 kB/s) and
  still validated. The bus glue was then given a STmin setting defaulting to 0, and both
  runs repeated without any override: Flow Control `30 08 00` throughout, 9.8 s each
  (29.4 kB/s), same outcomes.

### A-13 — UDS interop: the PC as a tester (CANable, udsoncan) · P1 · Module: e32p_uds · Status: Passed (11-bit 2026-10-06, 29-bit 2026-10-07)
- **What it proves:** the companion's ISO 14229-1 server satisfies a UDS client that shares
  none of our code — udsoncan over `can-isotp` and python-can — and holds the timing it
  announces: udsoncan times every answer with the server's own `P2` (50 ms) and `P2*`
  (5 s). (A-12 is kept for the manual reflash of example 33, slice 3.8.)
- **Setup:** CANable on the bus per `tests/hil/WIRING.md` §12; close Cangaroo.
- **Steps:** `pytest test_uds_canable.py --port COM5 --companion-port COM7 --canable COM8`.
- **Expected:** 2 passed, about 16 s. `test_uds_canable_full_flow` walks sessions, DIDs,
  security access, trouble codes (shared with OBD-II), erase, download, check and an ECU
  reset that really restarts the companion. `test_uds_canable_follows_response_pending`
  asks for a 7 s erase: udsoncan only gets through it if the server's first `0x78` comes
  inside `P2` and is repeated before `P2*` runs out.
- **Observed:** 2/2 on three runs; first `0x78` 1.5 ms after the request starts (host
  clock, adapter included), repeated 4001–4006 ms later, result at 7001–7006 ms. Both
  pass in the 29-bit sweep too (udsoncan on `NormalFixed_29bits`, 2026-10-07).
- **Note:** time host-side events with `time.perf_counter()`. On this PC
  `time.monotonic()` ticks every 15.6 ms.

### A-10 — CAN bitrate and addressing sweep · P2 · Module: e32p_isotp + e32p_j1939 + e32p_obd2 + e32p_uds · Status: Passed (2026-09-15: 500 k and 1 M — 57 passed, 4 skipped each. 2026-10-05: 29-bit normal-fixed — 65 passed, 4 skipped, OBD-II included. 2026-10-07: 29-bit normal-fixed — **76 passed, 4 skipped, twice, identical to the 11-bit gate**, UDS included; see tests/hil/BASELINE.md)
- **What it proves:** the whole CAN suite — J1939 and ISO-TP together — works at 500 kbit
  and 1 Mbit, and with 29-bit normal-fixed ISO-TP addressing sharing the bus with J1939.
- **Setup:** build and flash both boards from one variant: `build_500k` / `build_s3_500k`,
  `build_1m` / `build_s3_1m`, or `build_nf` / `build_s3_nf`, made from the
  `sdkconfig.defaults.rate500k`, `.rate1m` and `.addrfixed` overlays. CANable unplugged or
  at the same bitrate.
- **Steps:** run the full suite with `--can-bitrate 500000`, `--can-bitrate 1000000`, or
  `--isotp-addr normfixed`.
- **Expected:** the same pass/skip set as the 250 kbit gate in `tests/hil/BASELINE.md`.
- **Observed (2026-11-03):** after the security remediation, 29-bit normal-fixed
  (`build_nf` / `build_s3_nf`, CANable on the bus): **77 passed, 4 skipped**, the same
  as the 11-bit run the same day. See the regression record below.

### A-14 — Regression after the security remediation (slices A-J) · P1 · Module: all · Status: Passed (2026-11-03; Wi-Fi network tests pending owner action)
- **What it proves:** the security changes (J1939 TP checks, product-build guard, UDS
  lockout across resets, https-only OTA, signed example 33 images, one-time secret
  provisioning, example 29 and Wi-Fi settings clean-up, HIL host tooling, product
  overlay) did not break any build or test.
- **Steps:**
  1. Host Unity: `python tests\host_unity\run_host_tests.py`.
  2. Clean build of every touched build folder: `idf.py -C <project> [-B <dir>]
     [-D SDKCONFIG=...] clean build`, using each folder's own `SDKCONFIG` and
     `SDKCONFIG_DEFAULTS`: `tests/unit_runner`; the HIL agent (`build`, `build_esp32s3`,
     `build_nf`, `build_s3_nf`); examples 12, 23, 25, 26, 28, 29; example 33
     (`build_bench`, `build_ota1m`, `build_ota1m_b`).
  3. Flash `tests/unit_runner` to COM5 and read the Unity summary.
  4. HIL 29-bit: flash `build_nf` (COM5) and `build_s3_nf` (COM7), then run the full suite
     with `--isotp-addr normfixed` and the CANable.
  5. HIL 11-bit: flash `build` (COM5) and `build_esp32s3` (COM7), then run the full suite
     with the CANable. Leave both boards on these images.
  - Run `pytest` from a shell **without** the ESP-IDF environment, with the global Python
    (the global Python 3.12, `...\Programs\Python\Python312\python.exe`; it has
    `udsoncan`, the `.venv` does not). A flash counts only when the log shows "Hash of
    data verified" (the exit code alone is not enough).
  - The Wi-Fi tests (`test_net_mqtt.py`, `test_net_ota.py`) need the environment
    variables `HIL_WIFI_SSID` and `HIL_WIFI_PASS`. Without them they skip.
- **Expected:** host Unity UDS 100/0 and OBD-II 27/0; every build exit 0 with 0 warnings;
  unit runner `275 Tests 0 Failures 0 Ignored`; both HIL runs 77 passed, 4 skipped (the
  4 are the two deferred slave drivers and the two Wi-Fi tests).
- **Observed (2026-11-03):** all as expected. Host Unity UDS 100/0, OBD-II 27/0. 14 clean
  builds, 0 warnings each; the largest app (unit runner, 1293632 bytes) leaves 279232
  bytes in a 0x180000 slot, and the HIL agent leaves 311312 bytes. Unit runner 275/0.
  HIL 29-bit **77 passed, 4 skipped** (99.01 s); HIL 11-bit **77 passed, 4 skipped**
  (98.86 s). Both boards were left on the 11-bit agent images and answered `PING`.
- **Not covered:** the two Wi-Fi network tests (pending owner action) and the
  Secure Boot / Flash Encryption burns, which are owner-run on a sacrificial board
  (`docs/SECURITY_HARDENING.md`). Nothing here is a statement that the product is secure.
- **Details and size table:** `tests/hil/BASELINE.md`.

## Notes / open questions to fold in later

- Add SPI tests when `e32p_spi` lands (loopback via MOSI→MISO jumper; a real SPI
  device; transaction sizes).
- Add Wi-Fi / event coexistence tests at M5 (e32p_event adopting the Wi-Fi loop;
  ADC2-vs-Wi-Fi conflict from the analog milestone).
- Consider a host-side (Linux/QEMU) unit lane for pure-logic helpers that need no
  peripherals, to complement the on-target Unity runner.
- **TODO (bench hardware): pull-up on the CAN transceiver's D line.** The ESP32's TX
  pin is undriven from chip reset until the TWAI driver configures it. The HIL agent
  now parks the pin recessive in `app_main`, which leaves only the ROM and bootloader
  window (~300 ms) undriven; a 10 kΩ pull-up from the transceiver's D pin to 3.3 V
  covers that too, and is what a production ECU does. Check first whether the
  SN65HVD230 module already has one. Verify the same way this was found: run the
  DUT-side bus watcher (session scratchpad `diag_bringup/`) across resets with
  KingstVIS open — the bus must read recessive throughout. Background in
  `tests/hil/WIRING.md` §12.4. *2026-10-07: it matters beyond the boot window* — the
  companion parked in its ROM bootloader (TX undriven) corrupted 1 Mbit traffic until
  the DUT went bus-off (§12.5, diagnostics ticket slice 3.8).
