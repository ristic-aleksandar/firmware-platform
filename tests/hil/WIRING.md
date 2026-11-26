# HIL Wiring & Bench Manual

How to physically connect the rig for every hardware-in-the-loop test — from a
bare single board up to two boards on a live CAN bus. Single-board tests need
none of this; start at [§7](#7-running-each-suite) if you only have one board.

---

## 1. Rig tiers — what each setup unlocks

| Tier | You need | Unlocks |
|------|----------|---------|
| **1B** | one ESP32 + USB | all single-board tests (system, GPIO/analog/timer, bus loopbacks, protocol codecs, `*_neg`/`*_selftest`) |
| **1B + sensor** | + BMP280 (and/or TM1637) | `sensor_bmp280`, `sensor_tm1637` (`--with-sensors`) |
| **2B** | two ESP32 + USB ×2 + a few jumpers | `bus_uart_xfer`, `wl_espnow_xfer` (UART cross-wire / ESP-NOW) |
| **2B + XCVR** | + 2 CAN transceivers + 2×120 Ω | the whole CAN/J1939 group over a real bus (`can_*_xfer`, `can_j1939_*`, `can_tp_*`, `can_stress`) |
| **2B + net** | Wi-Fi + this PC as broker/server | `test_net_mqtt`, `test_net_ota` (`--wifi-ssid`) |

---

## 2. Bill of materials

**On hand (covers every tier above):**

| Qty | Part | Role |
|-----|------|------|
| 2 | ESP32-WROOM-32 DevKit | DUT + COMPANION (same agent binary) |
| 2 | CAN transceiver, 3.3 V (SN65HVD230 / VP230) | one per board for the CAN bus |
| 2 | 120 Ω resistor | CAN bus termination, one at each end |
| 1 | BMP280 (I²C, addr 0x76) | device-driver sensor test |
| 1 | TM1637 4-digit display | optional display test |
| 1 | USB-TTL adapter (v286) | spare serial: STM32/second console, or a PC-side Modbus/UART leg |
| 2 | MKS CANable V2.0 Pro (slcan firmware) | PC-side CAN: listen-only sniffing and ISO-TP interop (§12) |
| 1 | LA2016 logic analyzer, 16 ch, 200 MHz | physical-layer checks: CAN bit timing, I²C / SPI / TM1637 / WS2812 waveforms (§12) |
| — | power adapters, jumpers, resistors, caps | power + signal wiring |

**Still missing (and what it defers):**

| Missing | Defers |
|---------|--------|
| **`e32p_spi_slave` / `e32p_i2c_slave`** | two-board SPI/I²C (`bus_spi_xfer`, `bus_i2c_xfer`) — these stay skipped until the slave companion drivers are built (a software gap, not a parts gap) |

---

## 3. Power & USB

- **Simplest:** each board powered and controlled over its own USB cable to the
  PC. The DUT is the controlled board; the COMPANION's USB is read-only (the PC
  reads its `@EVT` stream to corroborate).
- **Power adapters:** use them when a board is placed away from the PC (e.g. the
  companion on the far end of the CAN bus). A board can be USB-powered for control
  *and* take 5 V from an adapter — just never back-feed 5 V into USB; pick one 5 V
  source per board. All logic is 3.3 V; the transceivers and sensors run off the
  board's **3V3** pin.
- **Common ground is mandatory** for every two-board or external-part connection.

---

## 4. Roles & provisioning

Both boards run the **same** agent firmware and read their role from NVS. Set it
once per board (over the serial monitor), it persists across reflashes:

```
@CMD SETROLE DUT          # board A — controlled by pytest
@CMD SETROLE COMPANION    # board B — autonomous peer, read-only to the PC
```

On boot each emits `@EVT READY role=<DUT|COMPANION>`. A COMPANION brings up **all**
its peer behaviours at once (CAN/J1939 on TWAI, UART echo on UART1, ESP-NOW on the
radio, Modbus slave on UART2) — they use independent peripherals, so they coexist.
Watch for pin clashes only if you build new *parallel* multi-channel tests.

---

## 5. Master pin map

Every signal the agent and demos use, in one place. Each peripheral is on its own
pins, so the interfaces compose without re-wiring (see [§6](#6-pin-conflict-matrix)).

Pins differ per board — the agent resolves them per target in
`agent/main/hil_pins.h`. Because boards join by wire/bus, a WROOM↔S3 pair works
fine with **different GPIO numbers on each end** — just wire each side to its own
column.

| Signal | WROOM-32 | ESP32-S3 | Used by |
|--------|:--------:|:--------:|---------|
| Console | UART0 (USB) | UART0 (USB) | control / `@EVT`, every board |
| TWAI TX | 21 | 4 | CAN/J1939 tests |
| TWAI RX | 22 | 5 | CAN/J1939 tests |
| I²C SDA | 13 | 8 | BMP280 (`sensor_bmp280`) |
| I²C SCL | 14 | 9 | BMP280 (`sensor_bmp280`) |
| UART1 TX | 17 | 17 | `bus_uart_xfer` (two-board echo) |
| UART1 RX | 16 | 18 | `bus_uart_xfer` |
| Modbus UART2 TX | 25 | 6 | `modbus_rtu_xfer` (§8.3) |
| Modbus UART2 RX | 27 | 7 | `modbus_rtu_xfer` (§8.3) |
| TM1637 CLK | 23 | 1 | `sensor_tm1637` |
| TM1637 DIO | 19 | 2 | `sensor_tm1637` |
| SPI SCLK/MOSI/MISO/CS | 18/23/19/5 | 10/11/12/13 | `bus_spi_loop` |
| I²S BCLK/WS/DATA | 26/32/33 | 14/15/16 | `bus_i2s_tx` |
| WS2812 DIN | 18 | 48 | `bus_ws2812` |

> **GPIO2 on the current DUT board is pulled down on the board** (measured 2026-10-05: reads
> 0 with only the internal pull-up and never rises; no LED is fitted to it). Push-pull
> output overrides it easily — fine for the agent's `GPIO_OUT` — but nothing that relies
> on a pull-up, open-drain or a floating input belongs on GPIO2 here. GPIO4 is clean.

> **UART port ≠ pins.** "UART1"/"UART2" are the ESP32 *peripherals*; the agent
> routes them to the GPIOs above via the GPIO matrix (any UART can use almost any
> pin). Note the WROOM's UART1 is deliberately **not** on its chip-default GPIO
> 9/10 — those are the SPI-flash pins — so the agent puts UART1 on 17/16 and UART2
> (Modbus) on 25/27. Wire by the **GPIO numbers in this table**, not by any
> "default UART pin" chart. (Assumes a plain WROOM-32; on a WROVER, GPIO16/17 are
> used by PSRAM.)

> **S3 has no DAC and a different touch peripheral**, so `periph_dac` and
> `periph_touch` report "skipped" on the S3 (and those two modules are disabled in
> its build). Everything else runs on both.

> **CAN and the BMP280 now coexist on both boards** — the WROOM's I²C was moved
> off the TWAI pins to GPIO **13/14** (S3 was already separate at 8/9), so you can
> leave the CAN transceiver wired while running the sensor test. No re-wiring.

> **Gateway note:** the `29_edge_gateway` flagship defaults its CAN pins to
> **GPIO5 (TX) / GPIO4 (RX)** — *different* from the agent's 21/22. When bench-testing
> the gateway's real-J1939 source (§9), either wire its transceiver to 5/4 or change
> the pins in `menuconfig → 29_edge_gateway`.

---

## 6. Pin conflict matrix

With the WROOM's I²C moved off the TWAI pins (to 13/14), the two-board rigs are on
independent pins and **all compose — nothing is mutually exclusive** anymore:

| Interface | WROOM pins | S3 pins |
|-----------|:----------:|:-------:|
| CAN/J1939 | 21/22 | 4/5 |
| I²C (BMP280) | 13/14 | 8/9 |
| UART echo | 17/16 | 17/18 |
| Modbus UART2 | 25/27 | 6/7 |
| TM1637 | 23/19 | 1/2 |
| ESP-NOW | RF | RF |

CAN + BMP280 + UART + Modbus + ESP-NOW can all be wired at the same time.

> **Permanently-wired interfaces get pins no single-board test drives.** The
> two-board jumpers stay connected across the whole suite (CAN 21/22, Modbus
> 25/27, UART echo 17/16, I²C 13/14, TM1637 23/19), so any single-board test that
> drove one of those pins would leak onto the wire. Hence I²S moved to the unwired
> loopback pins (26/32/33, off Modbus-TX 25 and TWAI-RX 22), and TM1637 sits on the
> SPI-loopback pins (CLK=23/MOSI, DIO=19/MISO) — `bus_spi_loop` asserts no data, so
> the display's load is harmless, while GPIO18 (PCNT) and GPIO4 (touch T0) stay clear.
> The remaining sharing is among **single-board** tests only (e.g. GPIO18 = SPI
> SCLK/LEDC/PCNT/WS2812); they run one-at-a-time with their own init/deinit, so it's
> never a conflict — only relevant if you build a new *parallel* multi-channel test.

---

## 7. Two-board wiring, per interface

### 7.1 CAN / J1939 (2B + XCVR) — the flagship bus

Per board, ESP32 ↔ transceiver (**WROOM** TWAI TX/RX = 21/22; **S3** = 4/5 —
each board just wires its own TWAI pins to its own transceiver, the bus is CANH/CANL):

```
 ESP32            SN65HVD230
 TWAI TX     ───► D (5)  (TXD)     (WROOM GPIO21 (G21) / S3 GPIO4 (IO4)) (orange wire)
 TWAI RX     ◄─── R (8)  (RXD)     (WROOM GPIO22 (G22) / S3 GPIO5 (IO5)) (white wire)
 3V3         ───► VCC (7)
 GND         ───► GND (6)
                  CANH (y) ──┐  to bus
                  CANL (g) ──┘
```

The bus between the two transceivers, **120 Ω at each end**, common ground:

```
   A: SN65HVD230              B: SN65HVD230
      CANH (3) ──────────────────────── CANH (3) (yellow wire)
                 │                  │
               [120Ω]             [120Ω]
                 │                  │
      CANL (2) ──────────────────────── CANL (2) (green wire)
   A.GND (6) ───────────── common ───────────── B.GND (6)
```

- **`Rs` must be tied to GND. `Vref` is left open.** On the SN65HVD230, `Rs` selects the
  mode: GND = high-speed (what this bench wants), a resistor to GND = slope control, and
  **VCC = standby, in which the driver is switched off**. A floating `Rs` is undefined and
  can sit high enough to land in standby — some breakout boards have an onboard pull-down
  and some do not, which is why a dangling pin can work for months and then stop. `Vref`
  is an output (VCC/2) and is meant to be unconnected.
- **What a standby transceiver looks like (2026-10-05):** every CAN test failed, including
  the single-board `isotp_self`, and a CANable on the bus saw **0 frames**. On the
  analyzer, the DUT's **G21 (TX) was busy** — one dominant pulse then a train of short
  retries — while **G22 (RX) sat flat recessive**. That combination means the MCU is
  transmitting and nothing is driving the bus: the controller never sees its own dominant
  bit echoed, raises a bit error and retries forever. Tying `Rs` to GND fixed it, and the
  suite went from 19 failures to green in one step.
- **Probe the logic pins, not CANH/CANL** (§12.4). Against a 1.65 V threshold the
  idle 2.5 V bus reads CANH permanently high and only CANL dipping, which looks like a
  broken channel and is just the differential pair seen through a single-ended threshold.
- 250 kbit/s (J1939 standard) is very forgiving on a short bench bus.
- The COMPANION broadcasts **PGN 65262** ~1 Hz once ready — the DUT's
  `can_j1939_rx` test consumes it. On B's console you'll see `@EVT COMPANION ready
  SA=0x21` then quiet until traffic.

### 7.2 UART cross-wire (2B, no transceiver) — `bus_uart_xfer`

Uses **UART1**, separate from the USB console. Cross TX↔RX, share ground:

```
   Board A (DUT)            Board B (COMPANION)
   UART1 TX ──────────────► UART1 RX (white)
   UART1 RX ◄────────────── UART1 TX (yellow)
   GND      ─────────────── GND
```
| Board | UART1 TX | UART1 RX |
|---|---|----------|
| ESP-WROOM-32 | GPIO17 (UART2 TX) | GPI16 (UART2 RX) |
| ESP-S3-WROOM-1 | GPIO17 - **IO17** | GPIO18 - **IO18** |

Per §5: **WROOM** UART1 TX/RX = **17/16**, **S3** = **17/18**. The companion
echoes whatever it receives on UART1.

### 7.3 ESP-NOW (2B, no wires) — `wl_espnow_xfer`

RF — nothing to wire. Power both boards on the same Wi-Fi channel (default). The
companion logs `@EVT ESPNOW …` when the DUT broadcasts.

---

## 8. Single-board sensor wiring (1B + sensor)

### 8.1 BMP280 (I²C) — `sensor_bmp280`

```
   ESP32              BMP280 (0x76)
   SDA ──────────────► SDA     (WROOM GPIO13 / S3 GPIO8)
   SCL ──────────────► SCL     (WROOM GPIO14 / S3 GPIO9)
   3V3 ──────────────► VCC
   GND ──────────────► GND
   3V3 ──────────────► SCB
   GND ──────────────► SDO
```

Most BMP280 breakouts have onboard pull-ups. I²C is now on its **own pins** (off
the TWAI pins), so you can leave the CAN transceiver connected. Addr is 0x76 (some
modules strap 0x77 — check the board).

**SDA is GPIO13, not GPIO12.** After the DUT board was swapped on 2026-10-02 the sensor
came back wired to GPIO12 and `sensor_bmp280` failed with `ESP_ERR_INVALID_STATE` and
`i2c_master_transmit_receive(): I2C transaction failed`. GPIO12 is a strapping pin
(MTDI, flash-voltage select) and the module's onboard pull-up holds it high at reset,
so that mistake is a boot hazard as well as a wrong pin.

### 8.2 TM1637 (2-wire) — `sensor_tm1637`

```
   ESP32              TM1637
   GPIO23 (CLK) ─────► CLK
   GPIO19 (DIO) ─────► DIO
   3V3          ─────► VCC
   GND          ─────► GND
```

Electrically write-only; the agent PASSes on clean writes — confirm the digits by eye.

### 8.3 Modbus RTU two-board (2B) — `modbus_rtu_xfer`

The DUT runs the `e32p_modbus` UART **master**; the COMPANION runs a Modbus
**slave** peer on **UART2** (holding registers `1000,2000,3000,4000` at unit id 1),
crossed:

```
   Board A (DUT, master)    Board B (COMPANION, slave)
   Modbus TX ─────────────► Modbus RX
   Modbus RX ◄───────────── Modbus TX
   GND       ─────────────── GND
```
| Board | Modbus TX | Modbus RX |
|---|---|----------|
| ESP-WROOM-32 | GPIO25 | GPIO27 |
| ESP-S3-WROOM-1 | GPIO6 - **IO6** | GPIO7 - **IO7** |

Use each board's own Modbus pins from §5: **WROOM** TX/RX = **25/27**, **S3** =
**6/7**. So a WROOM-DUT ↔ S3-companion pair wires WROOM.25→S3.7 and S3.6→WROOM.27.
9600 8E1, TTL-level (no RS-485 transceiver needed for a short bench link). It
coexists with the UART1 echo link (different UART/pins), so a board can be wired
for both at once. The DUT reads all four registers, writes+reads back one, and
checks an out-of-range read returns a Modbus exception — proving framing, CRC on
the wire, and inter-frame timing, not just the pure protocol.

---

## 9. Flagship gateway real-J1939 bench

Verify `29_edge_gateway`'s **real** field source (not the simulator) end-to-end,
using the dedicated **J1939 ECU simulator** (`examples/30_j1939_ecu_sim`) on board
B as the engine. It broadcasts ET1 (PGN 65262, coolant temp) and EEC1 (PGN 61444,
engine speed) at their normal cyclic rates — exactly the two PGNs the gateway
decodes. Each board stays honest: board B *is* an ECU (it broadcasts), the gateway
stays listen-only, and the HIL agent isn't involved.

> The HIL agent's COMPANION is **not** used here: its CAN peer is response-only (it
> broadcasts only when it receives a frame, to avoid bus-off when alone), so it
> won't feed a purely-listening gateway. The ECU sim is the real signal source.

1. Flash `examples/30_j1939_ecu_sim` to **board B**, with its CAN pins set to that
   board's TWAI GPIOs (`menuconfig → 30_j1939_ecu_sim → CAN TX/RX GPIO`: **21/22**
   on a WROOM, **4/5** on an S3). It broadcasts immediately (TWAI NO_ACK mode —
   the gateway tap is listen-only and never ACKs, so the sim must not depend on
   one); the console prints `ET1 coolant=.. C   EEC1 rpm=..` from the start.
2. Wire both boards onto the CAN bus per §7.1 (two transceivers, 2×120 Ω, common
   ground). The gateway board's transceiver goes on **its** CAN pins — either keep
   the agent's 21/22 by setting `menuconfig → 29_edge_gateway → CAN TX/RX GPIO` to
   21/22, or wire the gateway transceiver to the default 5/4.
3. Build the gateway with `menuconfig → 29_edge_gateway → Read a real J1939/CAN
   bus = y` (plus Wi-Fi + broker), flash it to **board A**.
4. Watch its MQTT telemetry — `src` flips from `sim` to `j1939`, and `coolant_temp`
   / `engine_speed` track the values the sim board is sending.

This closes the last open item on the gateway (TEST_PLAN I-32's hardware note).

> **Verified on hardware 2026-09-07.** Gateway boots `field source: j1939`; MQTT
> telemetry shows `"src":"j1939"` with `engine_speed` **and** `coolant_temp` both
> tracking the sim's sweep (rpm 700↔2500, coolant 70↔95 °C), flat heap, stable.

---

## 10. Running each suite

**Build + flash the agent per board target.** Easiest: the VS Code tasks
(`set-target`, `build`, `flash`, `clean build`, `full clean build`) — they now
prompt for a **Target chip**; pick `esp32` for the WROOM and `esp32s3` for the S3,
and the task keeps each chip's build dir + sdkconfig isolated automatically. Run
`set-target` once per chip, then `build`/`flash`.

By hand (what the tasks run): the WROOM uses the default `esp32` build; the S3
uses its own `build_esp32s3` dir + `sdkconfig.esp32s3` so the two never clobber
each other:

```powershell
# WROOM-32 (esp32) — the default build dir
idf.py -C tests/hil/agent set-target esp32
idf.py -C tests/hil/agent -p COM5 flash monitor          # then: @CMD SETROLE DUT

# ESP32-S3 — separate build dir + sdkconfig (defaults from sdkconfig.defaults.esp32s3)
idf.py -C tests/hil/agent -B tests/hil/agent/build_esp32s3 -D SDKCONFIG=sdkconfig.esp32s3 set-target esp32s3
idf.py -C tests/hil/agent -B tests/hil/agent/build_esp32s3 -D SDKCONFIG=sdkconfig.esp32s3 -p COM7 flash monitor
#   then: @CMD SETROLE COMPANION
```

Then run the suites from `tests/hil/host` (venv active). `--port` picks whichever
board is the DUT; a WROOM↔S3 pair works with either as DUT:

| Rig | Command |
|-----|---------|
| Single board | `pytest --port COM5 .` |
| + sensors (§8) | `pytest --port COM5 --with-sensors test_sensor.py` |
| Two boards | `pytest --port "COM5\|COM6" -m two_board .` |
| CAN group (§7.1) | `pytest --port "COM5\|COM6" -m "two_board and needs_xcvr" test_can.py` |
| Modbus RTU (§8.3) | `pytest --port COM5 --companion-port COM6 test_modbus.py` |
| Network (Wi-Fi) | `$env:HIL_WIFI_SSID = "<ssid>" ; $env:HIL_WIFI_PASS = "<password>"` then `pytest --port COM5 test_net_mqtt.py test_net_ota.py` |

DUT port first in the `"COM5|COM6"` pair. Close any serial monitor on a port first
— only one program can own it.

> **Run pytest from `tests/hil/host` with the venv active** (`pip install -r
> requirements.txt` once). Always give it a target — an explicit test file, or a
> trailing **`.`** when you don't (as above). Without one, pytest doesn't load
> this folder's `conftest.py`, so options like `--companion-port` are rejected.
> The VS Code tasks already do this. They run the global Python 3.12 (`py -3.12`),
> not the `.venv`.

---

## 11. Still to build

- **Full S3 single-board parity** — `e32p_dac` / `e32p_touch` / `e32p_adc` use
  ESP32-only driver APIs and are disabled in the S3 build (so `periph_dac`,
  `periph_touch`, `periph_adc` report "skipped" there). Porting each to be
  target-aware (S3 has no DAC; touch v2; curve-fitting ADC cali) would light them
  up on the S3 too. All other tests run on both boards.
- **STM32 pairings** — the STM32 can't run the ESP-IDF agent; it needs its own
  small peer firmware per bus (CAN node / Modbus slave / UART echo). The ESP32 DUT
  + Python host don't change. Adds real cross-vendor interop when built.
- **`bus_spi_xfer` / `bus_i2c_xfer`** — wait on the `e32p_spi_slave` / `e32p_i2c_slave`
  companion drivers (software, not parts).
- **Scope-based checks** — I²C/SPI/TM1637/WS2812 waveform timing. The CAN captures
  are done (§12.4).
- **CANable interop beyond ISO-TP** — OBD-II and UDS against the ESP32 as ECU, in
  Phases 2 and 3 of `docs/tickets/diagnostics-stack.md`. ISO-TP interop is automated
  already (§12.3).

---

## 12. CANable and logic analyzer

### 12.1 The CANable is a node on the bus

- **Match the bitrate.** The HIL agent runs CAN at 250 kbit by default; the sweep
  builds run at 500 kbit or 1 Mbit. A CANable at a different rate cannot decode the
  traffic, answers it with error frames, and knocks both ESP32s off the bus at random.
  That happened on 2026-09-14 and cost a full root-cause investigation
  (`BASELINE.md`, slice 1.5).
- **During ordinary HIL runs, close the program holding its port, or unplug it.** With
  its port closed the adapter is not active on the bus, at any bitrate. Don't rely on
  listen-only: this adapter's slcan firmware passes no frames through when opened
  listen-only (0 frames against 34 in normal mode for the same transfer, 2026-09-15), so
  a listen-only session proves nothing about the bus. And an adapter left open in
  normal mode at the right rate acknowledges every frame, which would hide a companion
  that has dropped off the bus from `test_can_stress`.
- **Wiring:** CANH and CANL to the bus, GND to the common ground. If your adapter has an
  on-board 120 Ω terminator (a switch or jumper), leave it off — the bus already has
  one at each end. Three terminators measure about 40 Ω across CANH/CANL instead of 60 Ω.
- **On this PC** it enumerates as **COM8** (USB ID `16D0:117E`, slcan firmware).

### 12.2 Sniffing by hand (Cangaroo)

Set the same bitrate as the bus. The ISO-TP capture in
`examples/31_isotp_echo/README.md` was taken with Cangaroo at 500 kbit. Expect to use
normal mode: with this adapter's slcan firmware, listen-only passes no frames through.
Close Cangaroo before running the automated tests below and before ordinary HIL runs:
the COM port can only be open in one program, and a closed port keeps the adapter off
the bus.

### 12.3 Automated CANable tests

```powershell
# From the repository root
cd tests\hil\host
.venv\Scripts\python.exe -m pytest test_isotp_canable.py test_obd2_canable.py test_uds_canable.py --port COM5 --companion-port COM7 --canable COM8
```

They need `python-can` and `can-isotp` (`requirements.txt`), and the UDS file also
`udsoncan`. Without them the tests
**skip on the missing import**, which looks like coverage and is not — check the skip
reasons, not just the pass count.

| Test | Adapter mode | What it proves |
|------|--------------|----------------|
| `test_canable_sniffs_ff_fc_cf` | normal (acknowledges, never transmits) | Records a 100-byte DUT ↔ companion exchange and checks every frame against ISO 15765-2 independently of both boards: First Frame length, flow control per block, sequence numbers, `0xAA` padding, and the payload bytes. |
| `test_canable_isotp_peer` | normal | The PC is the tester, using `can-isotp` — an ISO-TP implementation we did not write — and round-trips 100 bytes with the companion's echo peer. |
| `test_obd2_canable_pid_and_vin` | normal | The PC is an OBD-II scan tool: `01 0C` returns 1726 rpm and `09 02` returns the 20-byte VIN, whose multi-frame reassembly `can-isotp` performs. |
| `test_obd2_canable_broadcast_and_silence` | normal | An unsupported PID is answered `7F 01 12` physically; `01 0D` on 0x7DF returns 88 km/h **from the ECU's own 0x7E8**; the same unsupported PID on 0x7DF draws nothing for a full second. |
| `test_uds_canable_full_flow` | normal | The PC is a UDS tester with `udsoncan`, a client we did not write: sessions, the VIN, security access, a DID written and read back, a trouble code listed by UDS, an erase, a 4 KB download, the CRC check, and an ECU reset that really restarts the companion. |
| `test_uds_canable_follows_response_pending` | normal | A 7 s erase, longer than `P2*`: udsoncan only gets through if the first `0x78` comes inside `P2` and is repeated before `P2*` runs out. A listener notes when each one arrives. |

Each test opens the adapter and closes it again, so the CANable is off the bus for the
rest of the suite. All follow `--can-bitrate` and `--isotp-addr`.

### 12.4 LA2016 on the CAN lines

- **Probe the logic pins, not CANH/CANL.** The bus is differential; the transceiver
  TX/RX pins are clean 3.3 V logic that a CAN decoder reads directly.
  Suggested channels: CH0 = DUT TX (GPIO21), CH1 = DUT RX (GPIO22), CH2 = S3 TX (GPIO4),
  CH3 = S3 RX (GPIO5), analyzer GND to the common ground.
- **Sample fast enough.** At least 10× the bitrate; 10 MS/s covers every rate up to
  1 Mbit with margin. One bit lasts 4 µs at 250 kbit, 2 µs at 500 kbit, 1 µs at 1 Mbit.
- **What to capture for the diagnostics ticket:** one ISO-TP exchange at 250 kbit and one
  at 500 kbit, decoded. Measure the bit time (4 µs / 2 µs) and the gap between
  Consecutive Frames (the 1 ms STmin), which the CANable's PC timestamps cannot resolve.
- **The sample point is not visible on a logic analyzer.** It is where the controller
  samples inside each bit, set in firmware: both boards use the 40 MHz-crystal presets
  (`tseg_1 = 15`, `tseg_2 = 4`, 20 time quanta per bit), so they sample at 80 %.
- **How the captures were taken (2026-09-15):** KingstVIS at 10 MS/s, trigger on a falling
  edge on CH1, exported as CSV (one row per transition). A script on COM5 reset the DUT
  and ran `isotp_mf` every 3.8 s, so each 6–10 s window held a reset, a bring-up and a
  full 100-byte exchange. A Python script decoded every frame (bit de-stuffing, CRC-15,
  ACK) and measured the timing.
- **Results, with both fixes below in place:**

  | | 250 kbit | 500 kbit |
  |---|---|---|
  | Bit time, bus and both TX lines | 4.000 µs = 250 000 bit/s | 2.000 µs = 500 000 bit/s |
  | Frames with correct CRC, ACK and EOF | 68 of 68 | 102 of 102 |
  | CF spacing, DUT → S3 (STmin 1 ms) | 2.000 ms typical, 1.53 ms minimum | 2.000 ms typical, 1.50 ms minimum |
  | CF spacing, S3 → DUT | 5.0 ms typical, 4.23 ms minimum | 5.0 ms typical, 4.26 ms minimum |
  | CF gaps shorter than STmin | 0 of 48 | 0 of 72 |

- **What the first capture found, now fixed:** the first two Consecutive Frames of every
  block went out 12 µs apart despite STmin 1 ms, and every TWAI bring-up held the bus
  dominant for 8 ms. Details in `docs/tickets/diagnostics-stack.md` §2.3.
- **A connected probe biases a pin that nothing drives yet (2026-09-16).** Both captures
  showed the bus dominant for 2.1–2.3 s after every DUT reset. That was the analyzer.
  GPIO21 floats from chip reset until `e32p_twai_init()` configures it, and the CH0 probe
  holds that floating line near the configured threshold (1.65 V), which the transceiver
  reads as dominant. Measured from the DUT, probe attached, firmware unchanged:

  | KingstVIS | Resets | Bus after boot |
  |-----------|--------|----------------|
  | closed (not running) | 10 | recessive 10/10 |
  | open, capture stopped | 10 | dominant 10/10 |
  | open, armed | 10 | dominant 10/10 |

  So it is not about capturing: **running the software is enough**, because it loads the
  analyzer's firmware and sets the threshold on connect. Close KingstVIS to release the
  line. The firmware never jammed the bus, but the pin really is undriven in that window.
- **Since 2026-09-16 the agent parks its CAN TX pin recessive** at the top of `app_main`,
  so this artifact no longer shows up once the app is running: with KingstVIS open, 10
  resets read recessive from ~1 ms after app start, where they used to stay dominant for
  seconds. The ROM and bootloader window (~300 ms) is still undriven — a pull-up on the
  transceiver's D line would cover that, a TODO in `docs/TEST_PLAN.md`.

### 12.5 A parked board corrupts the bus

A board that is wired to the bus but is not running firmware that drives its CAN TX pin
— held in its ROM bootloader, for example — leaves the transceiver's D input floating.
**Measured 2026-10-07 at 1 Mbit:** with the companion parked that way, every UDS download
from the CANable to the DUT drew bus errors from the first frames on, and the DUT went
bus-off within 3 s (its TEC and REC both climbing). With the companion running an image
that drives TX, the same downloads ran with zero bus errors. The idle bus showed nothing,
so this only bites under traffic.

- To keep a board off the bus, **unplug its transceiver or power it off** — don't park
  it in its bootloader.
- To run a test at another bitrate, put **every** board on firmware at that bitrate.
- The real fix is the pull-up on the transceiver's D line (TODO in `docs/TEST_PLAN.md`).

---

## 13. Flashing: the EN capacitor on these DevKits

Both WROOM-32 DevKits on this bench need a **10 µF capacitor between `EN` and `GND`**
to flash without holding the BOOT button. Fit it on the breadboard; if it is
electrolytic, the **+ leg goes to `EN`**. Without it, flashing fails to sync and only a
manual BOOT-hold works.

**Why.** Auto-reset is a cross-coupled transistor pair driven by the USB bridge's DTR
and RTS lines. Measured on the LA2016 (CH0 = `EN`, CH1 = `IO0`, 2026-10-02):

| DTR | RTS | EN | IO0 |
|-----|-----|------|------|
| off | off | high | high |
| **on** | off | high | **low** |
| off | **on** | **low** | high |
| **on** | **on** | high | high |

The last row is the problem. esptool resets by going from row 3 (chip held in reset) to
row 2 (`IO0` low), and each line change is a separate call into the Windows CP210x
driver — **measured 2.002, 2.004 and 2.114 ms apart**. In between, the pair passes
through row 4, where neither transistor conducts and `EN` is released. In that window
the capture shows `EN` crossing the 1.65 V threshold in **under 6 µs**, so the chip
leaves reset, reads `IO0` high and runs the flash bootloader. `IO0` only falls 2 ms
later, long after the strap was sampled.

The capacitor holds `EN` below the threshold across that gap, which is what the stock
DevKit design assumes. Nothing else is needed: with 10 µF fitted, esptool's default
timing gives `boot:0x23 (DOWNLOAD_BOOT(UART0/UART1/SDIO_REI_REO_V2))` and
`waiting for download`, so `reset_delay` does not have to be raised.

- **Use 10 µF, not 100 µF.** With the board's ~10 kΩ pull-up, 100 µF keeps `EN` low for
  roughly a second — the chip is still in reset when the flasher starts talking.
- **This is a timing margin, not a fault.** The circuit, the DTR path, the driver and
  the firmware all measured healthy. The same bench flashed fine for weeks before the
  margin tipped, and a replacement board of the same design failed identically.
