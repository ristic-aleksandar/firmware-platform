# HIL Baseline — pre-slice-1.0 reference run

Captured **2026-09-09**, before any edit from the diagnostics-stack work item
(`docs/tickets/diagnostics-stack.md`, slice 1.0 step 0).

This file exists because slice 1.0 changes `CONFIG_FREERTOS_HZ` from 100 to 1000
and raises both TWAI queue depths on the HIL agent. The regression gate for that
change is **differential** — the post-change run must produce the same pass/skip
set as the table below. Comparing against a remembered number ("48 passed")
would fail silently if the number were wrong; comparing against a captured set
does not.

## Rig

| | |
|---|---|
| DUT | ESP32-WROOM-32, COM5, `build/hil_agent.bin` (2026-09-02 14:28) |
| COMPANION | ESP32-S3, COM7, `build_esp32s3/hil_agent.bin` (2026-09-02 14:28) |
| Firmware | Unmodified — both binaries newer than the newest agent source (14:13) |
| Wiring | Full bench: CAN both ends + 2×120 Ω + common ground, Modbus UART2 cross-wire, BMP280, TM1637 |
| Command | `.venv\Scripts\python.exe -m pytest . --port COM5 --companion-port COM7 -v -ra` |
| Result | **48 passed, 4 skipped, 0 failed** in 20.21 s |
| Toolchain | Python 3.12.10, pytest 9.1.1, pytest-embedded 2.8.1 |

## Expected set

52 collected. Any deviation from this table after slice 1.0 triggers the abort
procedure in the ticket's §9.

| File | Test | Baseline |
|------|------|----------|
| `test_ble.py` | `test_ble_selftest` | PASS |
| `test_bus.py` | `test_bus_uart_loop` | PASS |
| `test_bus.py` | `test_bus_i2c_bus` | PASS |
| `test_bus.py` | `test_bus_spi_loop` | PASS |
| `test_bus.py` | `test_bus_i2s_tx` | PASS |
| `test_bus.py` | `test_bus_ws2812` | PASS |
| `test_bus.py` | `test_bus_uart_xfer` | PASS |
| `test_bus.py` | `test_bus_spi_xfer` | SKIP — needs `e32p_spi_slave` companion driver |
| `test_bus.py` | `test_bus_i2c_xfer` | SKIP — needs `e32p_i2c_slave` companion driver |
| `test_can.py` | `test_can_twai_neg` | PASS |
| `test_can.py` | `test_can_twai_selftx` | PASS |
| `test_can.py` | `test_can_j1939_codec` | PASS |
| `test_can.py` | `test_can_j1939_neg` | PASS |
| `test_can.py` | `test_can_tp_loop` | PASS |
| `test_can.py` | `test_can_tp_neg` | PASS |
| `test_can.py` | `test_can_acl_contend` | PASS |
| `test_can.py` | `test_can_twai_xfer` | PASS |
| `test_can.py` | `test_can_j1939_single` | PASS |
| `test_can.py` | `test_can_j1939_rx` | PASS |
| `test_can.py` | `test_can_tp_bam` | PASS |
| `test_can.py` | `test_can_tp_cmdt` | PASS |
| `test_can.py` | `test_can_stress` | PASS |
| `test_modbus.py` | `test_modbus_selftest` | PASS |
| `test_modbus.py` | `test_modbus_rtu_xfer` | PASS |
| `test_mqtt.py` | `test_mqtt_selftest` | PASS |
| `test_net_mqtt.py` | `test_net_mqtt` | SKIP — no `--wifi-ssid` |
| `test_net_ota.py` | `test_net_ota` | SKIP — no `--wifi-ssid` |
| `test_ota.py` | `test_ota_query` | PASS |
| `test_periph.py` | `test_periph_gpio` | PASS |
| `test_periph.py` | `test_periph_ledc` | PASS |
| `test_periph.py` | `test_periph_adc` | PASS |
| `test_periph.py` | `test_periph_dac` | PASS |
| `test_periph.py` | `test_periph_touch` | PASS |
| `test_periph.py` | `test_periph_pcnt` | PASS |
| `test_periph.py` | `test_periph_gptimer` | PASS |
| `test_pm.py` | `test_pm_wakecause` | PASS |
| `test_pm.py` | `test_pm_lightsleep` | PASS |
| `test_sensor.py` | `test_sensor_bmp280` | PASS |
| `test_sensor.py` | `test_sensor_tm1637` | PASS |
| `test_smoke.py` | `test_agent_alive` | PASS |
| `test_smoke.py` | `test_sysinfo` | PASS |
| `test_smoke.py` | `test_list_contains_ping` | PASS |
| `test_smoke.py` | `test_run_ping_passes` | PASS |
| `test_smoke.py` | `test_run_sysinfo_passes` | PASS |
| `test_smoke.py` | `test_run_unknown_errors` | PASS |
| `test_smoke.py` | `test_setrole_persists` | PASS |
| `test_sys.py` | `test_sys_event` | PASS |
| `test_sys.py` | `test_sys_nvs` | PASS |
| `test_sys.py` | `test_sys_wdt` | PASS |
| `test_wireless.py` | `test_wl_wifi_neg` | PASS |
| `test_wireless.py` | `test_wl_espnow_neg` | PASS |
| `test_wireless.py` | `test_wl_espnow_xfer` | PASS |

## Slice 1.0 result — PASSED (2026-09-09)

Re-run after `CONFIG_FREERTOS_HZ` 100 → 1000, `E32P_TWAI_RX_QUEUE_LEN` 5 → 32 and
`E32P_TWAI_TX_QUEUE_LEN` 5 → 64, both boards reflashed from the rebuilt binaries:

**48 passed, 4 skipped, 0 failed in 20.19 s** — identical set to the table above,
identical skip reasons. Differential gate satisfied.

Firmware attribution confirmed via `@CMD SYSINFO` on the S3 (`uptime=2264257ms`,
`reset=power-on`), consistent with the flash that started that uptime window.
Worth noting for future gates: the pass set alone cannot distinguish new firmware
from old, since an unchanged set is exactly what the gate expects. Attribution
has to come from outside the test output.

## Slice 1.5 result — PASSED (2026-09-14), after one environmental incident

**First attempt.** Slice 1.5 firmware on both boards. Run 1: 48 passed, 4 skipped.
Run 2, about 20 s later with the S3 not reset: `test_can_j1939_single`,
`test_can_tp_bam` and `test_can_tp_cmdt` failed with the companion silent, and
`test_can_stress` failed with `sent 90/1000`. The abort procedure was followed: no fix,
no next slice, root-cause analysis first. The code analysis came back inconclusive — it
found no mechanism in slice 1.5 or slice 1.0.

**Root cause, confirmed by an A/B test on the bench.** A CANable was still on the bus at
500 kbit from an earlier capture while the HIL suite ran at 250 kbit. At 500 kbit the
tests failed; with the same CANable set to 250 kbit they passed. The cause was the bench,
not the firmware.

**Bench rule that follows.** During a HIL run the CANable must be unplugged, or at the HIL
bitrate in **listen-only** mode. *(Corrected 2026-09-15: this adapter's slcan firmware
passes no frames through in listen-only, so the reliable rule is to close the program
holding its port, or unplug it. See `WIRING.md` §12.1.)* At the right bitrate but in normal mode it acknowledges
frames, which hides a companion that has dropped off the bus from `test_can_stress`
(that test needs only a hardware acknowledgement).

**A real, older defect found on the way, fixed in the same gate.** `comp_can_keepalive` only
acted in `BUS_OFF`, waited at most 500 ms for recovery, and called `twai_start()` once. If
recovery took longer, that call failed and nothing retried, so the companion stayed off the
bus until a reset. It now acts on each state as it sees it (`BUS_OFF` → start recovery,
`STOPPED` → `twai_start()`) and no longer blocks. It was not the cause of the incident above.

**Final gate.** Slice 1.5 plus the recovery fix on both boards, CANable unplugged or in
listen-only, two runs back to back without resetting the S3: **both passed** (reported by the
developer; the summary lines were not captured here). The optional forced bus-off check of
the recovery fix was not reported.

## Slice 1.6 result — PASSED (2026-09-15)

Slice 1.6 adds the diagnostics HIL group: a real ISO-TP peer on the companion and 7 new
tests (`test_isotp.py`). The set is now **59 collected: 55 passed, 4 skipped** — the 48
above, unchanged, plus all 7 ISO-TP tests. Both boards were flashed by Claude from a tool
call (auto-reset into the bootloader now works on both), with the CANable unplugged or in
listen-only.

**First run: 5 ISO-TP tests failed, for two reasons, both fixed before the gate:**

1. **Engine timer bug.** A channel created long after boot and used straight away armed its
   flow-control timer from time 0, because the engine had never been polled. Its first poll
   at the real uptime then expired the timer at once, so every multi-frame transfer failed
   with `ESP_ERR_TIMEOUT` a few ms in. Single frames arm no timer, which is why
   `isotp_sf` and `isotp_ncr` passed. Example 31 had only escaped it because its first send
   happened under 1 s after boot. Fixed in `e32p_isotp_poll()` (timers are moved onto the
   real clock on the first poll), with a new Unity case that failed before the fix and
   passes after it.
2. **Host test race.** `_verdict()` in `test_isotp.py` could match a verdict line before the
   whole line had arrived over serial, cutting off its `rate=` / `addr=` tail. The pattern
   now requires the line ending.

**Gate.** Two runs back to back without resetting the S3: **55 passed, 4 skipped** both
times (24.2 s, 24.4 s). The unit runner on the WROOM, flashed and read the same day: **133
tests, 0 failures, 0 ignored**, including all 31 ISO-TP cases and the new first-poll case.

## A-10 sweep — 29-bit normal-fixed run PASSED (2026-09-15)

Both boards flashed from `build_nf` / `build_s3_nf` (250 kbit, ISO-TP normal-fixed 29-bit
addressing), full suite with `--isotp-addr normfixed`: **55 passed, 6 skipped**. The 6 skips
are the 4 above plus the 2 CANable tests (no `--canable`). All 13 J1939 tests and all 7
ISO-TP tests passed with both stacks on 29-bit IDs on one bus, which is the real test of the
companion's PF-byte router; every ISO-TP verdict reported `addr=normfixed`, so the run is
attributable to that image.

## A-09 and A-10 — CANable interop and the rate sweep, PASSED (2026-09-15)

Every run below: both boards flashed by Claude, then the full suite with
`--canable COM8` (Cangaroo closed). **61 collected** = the 48 baseline tests + 7 ISO-TP +
2 CANable interop tests.

| Build (both boards) | Command options | Result |
|---------------------|-----------------|--------|
| `build_500k` / `build_s3_500k` | `--can-bitrate 500000` | **57 passed, 4 skipped** (29.6 s) |
| `build_1m` / `build_s3_1m` | `--can-bitrate 1000000` | **57 passed, 4 skipped** (29.5 s) |
| `build` / `build_esp32s3` (default, 250 kbit, 11-bit) | — | **57 passed, 4 skipped** (29.7 s) |
| `build_nf` / `build_s3_nf` (250 kbit, 29-bit normal-fixed) | `--isotp-addr normfixed` | 55 passed, 6 skipped without `--canable`; then `test_isotp_canable.py` alone: **2 passed** |

The 4 skips are the original four (two slave-driver tests, two network tests). J1939 and
ISO-TP pass together at every rate, and with both on 29-bit IDs. The PC, using `can-isotp`,
round-trips 100 bytes with the companion in both addressing modes and at every rate.

**Found on the way:** the CANable's slcan firmware passes no frames through in listen-only
mode — 0 frames against 34 in normal mode for the same transfer. The sniff test now opens
the adapter in normal mode (it acknowledges frames, never transmits, and everything it
asserts is frame content). The bench rule is corrected in `WIRING.md` §12.1: close the
program holding the adapter's port, or unplug it. The boards were left on the default image.

## STmin pacing fix — PASSED (2026-09-15)

The LA2016 capture at 250 kbit showed the first two Consecutive Frames of every block
12 µs apart despite STmin 1 ms (ticket §2.3). After the engine fix, both boards were
flashed with the default image (250 kbit, 11-bit) and checked by Claude:

| Check | Result |
|-------|--------|
| Unit runner on the WROOM | **135 tests, 0 failures, 0 ignored** (2 new STmin cases) |
| Full suite, `--canable COM8` | **57 passed, 4 skipped** (30.0 s) — same as before the fix |

Before the fix, the same image had run the 100-byte echo (`isotp_mf`) 1800 times in a row
over one hour while the capture was taken: 1800 passed. The echo peer does not check
CF spacing, which is why only the logic analyzer caught the defect.

## TWAI bring-up fix — PASSED (2026-09-15)

The same capture showed the DUT holding the bus dominant for 7.95 ms at every TWAI
bring-up (ticket §2.3). `e32p_twai_init()` now presets the TX pin's output register bit to
1 before the IDF install. Both boards flashed with the default image, checked by Claude:

| Check | Result |
|-------|--------|
| Unit runner on the WROOM | **136 tests, 0 failures, 0 ignored** (new `e32p_twai` case) |
| Full suite, `--canable COM8` | **57 passed, 4 skipped** (30.0 s) |

## LA2016 captures — 250 k and 500 k, PASSED (2026-09-15)

Both captures were taken with the two fixes above in place; the measurements are in
`WIRING.md` §12.4. During the captures a script reset the DUT and ran `isotp_mf` every
3.8 s:

| Loop | Cycles (DUT reset + 100-byte echo) | Result |
|------|-------------------------------------|--------|
| 250 kbit | 186 | 186 passed |
| 500 kbit | 1846, over four runs | 1846 passed |

Afterwards both boards went back to the default image (250 kbit, 11-bit) and the full
suite ran with `--canable COM8`: **57 passed, 4 skipped** (30.1 s).

## CAN TX parked at boot — verified (2026-09-16)

The captures' "bus dominant after every DUT reset" was the analyzer probe biasing a pin
nothing drives yet (`WIRING.md` §12.4). The agent now parks its CAN TX pin recessive at the
top of `app_main`. Measured from the DUT with the probe attached and **KingstVIS open**,
10 resets each:

| Agent | Bus after boot |
|-------|----------------|
| before | dominant for the whole 4 s window, 10/10 |
| after | recessive from ~1 ms after app start, 10/10 |

Both boards then went back to the default image: **57 passed, 4 skipped** (29.9 s).

## Bench incident — 2026-09-17/18 (closed: 57 passed, 4 skipped)

After the analyzer session the suite fell to **17 failed, 40 passed**: every test that
needs a wire between the boards, plus `isotp_self`. Three separate faults, found one
at a time:

| Fault | Symptom | Proof | Status |
|-------|---------|-------|--------|
| UART jumper loose | `test_bus_uart_xfer` fails | passed once reseated | fixed |
| One **120 Ω terminator disconnected** | companion (S3) crash-loops about once a second: `Interrupt wdt timeout on CPU0` | backtrace decodes to the TWAI ISR spinning at `twai.c:205` (`twai_intr_handler_main` entering its critical section); gone once the terminator was back — 0 panics in 10 s | fixed |
| **DUT browns out when its radio starts** | `test_wl_espnow_xfer` fails 3/3; the DUT restarts right after `phy_init` | `SYSINFO` reports `reset=power-on` before the test and **`reset=brownout`** after it; IDF's brownout handler sets that reason (`brownout.c:50`) before its software reset | fixed — developer changed the DUT's USB power; afterwards the DUT survives the test (uptime keeps counting, `reset=power-on`) |
| S3's radio state stale after the crash loop | ESP-NOW still failed with the DUT now broadcasting; the companion reported nothing | cured by one hard reset of the S3 (it does not reset when its port opens), then 3/3 wireless passed twice | fixed |

With the terminator back: 56 passed, 1 failed (ESP-NOW), 4 skipped. After the DUT
power fix and one S3 reset: **57 passed, 4 skipped** (30.2 s) — the baseline again.

Two notes for next time. ~~An unterminated bus is not just lost frames here: it drove the
companion's TWAI driver into a state where its own ISR deadlocked.~~ **Corrected
2026-10-05: not supported.** A deliberate reproduction with all termination removed gave
zero panics; the crash was real but its cause is unknown — see "TWAI interrupt-watchdog
crash — not reproduced" below. And a `SW_RESET` with a garbled byte right after
`phy_init` is the brownout handler's reset, not a software bug — `SYSINFO`'s `reset=`
field says which.

## Auto-reset into download mode — fixed (2026-10-02)

The DUT stopped entering download mode on its own: every flash needed the BOOT button
held down. A replacement WROOM-32 behaved identically, so it was not the board.

Ruled out by measurement before anything was changed: the **firmware** (nothing in the
tree touches GPIO0, and there is no `gpio_hold_en` or deep-sleep pad hold anywhere, so
no pad stays driven across a reset); the **PC** (the CP210x driver instance dates from
2026-08-06 and is unchanged since the bench was flashing fine on 09-15); and the
board's **reset circuit**, which the analyzer showed working exactly as designed.

Root cause and fix are in `WIRING.md` §13: the cross-coupled pair releases `EN`
whenever both DTR and RTS are asserted, the driver takes ~2 ms between those two line
changes, and `EN` rose through its threshold in under 6 µs inside that window — so the
strap was sampled 2 ms before `IO0` went low. **10 µF between `EN` and `GND`** on the
DUT restores the margin the design assumes.

Verified after fitting: `boot:0x23 (DOWNLOAD_BOOT(UART0/UART1/SDIO_REI_REO_V2))` with
esptool's default 50 ms hold, `esptool --port COM5 chip-id` connects and runs the stub
flasher with no button, and the slice 2.4 agent flashed hands-free — the board lists
back 58 test ids including the seven `obd2_*`.

## Slice 2.4 result — PASSED (2026-10-02)

The OBD-II HIL lane went green on its first run: **6/6** (`obd2_pid_phys`,
`obd2_pid_func`, `obd2_unsupported`, `obd2_vin`, `obd2_dtc_cycle`, `obd2_freeze`),
DUT as scan tool against the companion's OBD-II server, 250 kbit, 11-bit normal.

Full suite afterwards: **61 passed, 5 skipped, 27.6 s** — the new baseline number.
One failure on the way there, `sensor_bmp280`, was the BMP280's SDA left on GPIO12
after the DUT board swap; moved to GPIO13 it passes (see `WIRING.md` §8.1).

The 5 skips: `test_isotp_canable` (no `python-can` in this interpreter), two deferred
companion drivers (SPI/I²C slave), and the two network tests that need `--wifi-ssid`.
Note the CANable lane is *not* being exercised in this configuration — it skips on the
missing module, not on hardware.

## Slice 2.5 result — PASSED (2026-10-02)

`test_obd2_canable.py` adds the PC as an OBD-II scan tool over `can-isotp`: 2/2 on the
first run. Full suite with the CANable lane: **65 passed, 4 skipped, 41.9 s** — the
current baseline number.

`python-can` 4.6.1 and `can-isotp` 2.0.7 are now installed in the main Python 3.12
(not a venv, which `requirements.txt` asks for). Before that, `test_isotp_canable.py`
was skipping on the missing import — it looked like coverage and was not.

One intermittent failure was seen on 2026-10-02 and has not recurred: the companion
rejected `RUN obd2_dtc_seed` with `@ERR expected @CMD prefix`, once in 10 full-suite
runs. The agent now echoes the offending line so a recurrence identifies itself, and
`flush()` terminates a stale partial line before clearing. Cause plausible, not proven.

## Slice 2.6 — both sweeps PASSED (2026-10-05)

| Build | Addressing | Result |
|-------|-----------|--------|
| `build` / `build_esp32s3` | 11-bit normal | **65 passed, 4 skipped** (41.9 s) |
| `build_nf` / `build_s3_nf` | 29-bit normal-fixed | **65 passed, 4 skipped** (42.1 s) |

Identical pass/skip sets, OBD-II included. The 29-bit run is the one that matters: it is
the only configuration where J1939 and the diagnostics stack are both 29-bit on one live
bus, and it exercises the slice 2.3 glue fix (a functional channel sourced from 0x33,
answering from the ECU's own ID) end to end. Both boards were reflashed to the default
11-bit images afterwards and re-gated green.

### The bench detour that preceded it (closed)

A first attempt at the sweep returned 19 failures. They were worthless as evidence: the
same 11-bit binary that had just passed also failed after being re-flashed, so the fault
was the bench, not the build. Three faults, found in order:

| Fault | How it showed | Proof |
|-------|---------------|-------|
| 120 Ω terminators disconnected | every CAN test fails | reconnected; still failed, so not the whole story |
| **DUT transceiver `Rs` left floating → standby** | even single-board `isotp_self` fails; CANable sees **0 frames** | analyzer: DUT **G21 (TX) active** with retry bursts, **G22 (RX) flat recessive**. Tying `Rs` to GND → 2 passed in 1.6 s, then 61 board tests green |
| CANable **CANH/CANL swapped** | only the 4 CANable tests fail; boards fine | leads corrected → 4 passed |

Lesson worth keeping: `isotp_self` runs in NO_ACK mode and needs no cooperation from the
companion, so it is the fastest way to tell a bench fault from a code fault. When it
fails, stop reading the other 60 results. One caveat learned later the same day: another
node with a broken receive path can still make it fail (see the RX-wire observation below).

Also seen, not yet fixed: `@EVT` lines can be **interleaved with `ESP_LOG` output** during
boot — one companion line arrived as `EVT ISOTP peer ready` with the `@` missing, and
another as `@EVT` followed by a log line. Harmless at boot, but it is the same shape as
the one-off `@ERR expected @CMD prefix` seen on 2026-10-02, and worth a proper fix.

## Console output lock — fixed (2026-10-05)

Protocol lines and `ESP_LOG` output share UART0 but reached it by different routes:
`hil_emit()` writes through the UART driver, `ESP_LOG` through `vprintf`/stdout. With no
common lock a log line could land inside a protocol line. Caught on the companion's boot,
where Wi-Fi and ESP-NOW log heavily while the peers emit their ready lines — one line
arrived as `EVT ISOTP peer ready` with the leading `@` eaten, another as a bare `@EVT`
followed by log text. A host waiting on `@EVT ...` simply never sees it.

Fix: one recursive mutex taken by both paths (`hil_out_init()` in `hil_agent.c` installs
an `esp_log_set_vprintf()` sink that takes it, and `hil_emit()` takes it around its
write). The log sink flushes stdout inside the lock, or an unflushed tail would surface
later inside the next line. The take is capped at 100 ms and never drops output — a
mangled line still beats a missing one — and it is skipped in ISR context.

Measured after: **35 protocol lines over 5 companion resets, 0 broken**, and 5 more over
5 DUT resets. Before the fix, a single companion boot showed 2 broken lines. Full suite
unchanged at 65 passed, 4 skipped.

This is also the most likely explanation for the one-off `@ERR expected @CMD prefix`
recorded above, though that was never reproduced and so is not proven.

## TWAI interrupt-watchdog crash — not reproduced (2026-10-05)

On 2026-09-17 the companion crash-looped with `Interrupt wdt timeout on CPU0`, backtrace in
`twai_intr_handler_main` at `twai.c:205`, and it stopped when a terminator was reconnected.
That was recorded as "an unterminated bus deadlocked the TWAI ISR". A deliberate attempt to
reproduce it does not support that.

**From the source (IDF 5.3.1):**
- `twai_intr_handler_main` is straight-line — no loops, no waits. It cannot deadlock on its
  own; line 205 is its first statement (`portENTER_CRITICAL_ISR`), so a backtrace there
  means only "the CPU was in this ISR when the watchdog fired".
- Its one expensive path, `twai_alert_handler` calling `ESP_EARLY_LOGx` inside the spinlock,
  is dead code here: it runs only when `TWAI_ALERT_AND_LOG` is enabled, and `e32p_twai`
  leaves `alerts_enabled` at `TWAI_ALERT_NONE`.
- The bus-error interrupt is part of the hardcoded `DRIVER_DEFAULT_INTERRUPTS` (`0xE7`) and is
  not reachable from `twai_general_config_t`. An application cannot mask it, which limits
  what any mitigation could look like.

**On the bench**, 250 kbit, both boards on matched images, companion console watched for panics:

| Condition | Companion panics | Traffic result |
|-----------|-----------------|----------------|
| DUT jamming at the wrong bitrate (500 k node on the 250 k bus) | 0 in 35 s | — |
| One bench terminator out | 0 | `isotp_self` 20/20 |
| Both bench terminators out, CANable off the bus | 0 | 20/20 |
| All termination out, including the CANable's own | 0 | 20/20, and 30/30 two-board (companion transmitting) |
| Companion's CAN RX wire (GPIO5) pulled, floating | 0, alone and under traffic | two-board tests fail, as they must |

The bus is indifferent to termination at this length — 20 cm at 250 kbit, where a bit is
4 µs and reflections settle long before the sample point — so the September terminator
was most likely coincident, not causal. **The crash was real; its cause is unknown.** No
code change is proposed for a failure that cannot be triggered.

**Two observations made along the way:**
- With the companion's RX wire off, the **DUT's own NO_ACK self-test failed** as well, and
  passed again once the wire was back. One node that cannot hear the bus disrupts it for
  the others — plausibly by misreading the bus and sending error frames. Not captured on
  the analyzer, so the mechanism is unconfirmed.
- `can_stress` sends 1000 frames with a 100 ms transmit timeout each. With no node able to
  acknowledge, the queue fills after 64 and the rest each wait out the timeout: about
  **94 s to fail**. Bounded, but slow enough to look like a hang; it should give up after
  a few consecutive timeouts.

**If it recurs:** run `host/console_log.py COM7 --elf ../agent/build_esp32s3/hil_agent.elf`
for the whole failing run. It opens the port with DTR and RTS already low, so watching
does not reset the board, and at the end it prints the `addr2line` command that decodes
any backtrace against the image that was running. Verified 2026-10-05: 328 lines logged
from the companion under traffic, no reset on open.

## `can_stress` fails fast on a dead bus (2026-10-05)

`can_stress` queues 1000 frames with a 100 ms transmit timeout each. With no node able to
acknowledge, the 64-deep TX queue fills and every later attempt waits out its timeout —
about 94 s to reach a FAIL, long enough to pass for a hang. It now gives up after **5
consecutive** timed-out transmits and says why.

The pass criterion is unchanged: all 1000 frames sent. On a healthy bus a full queue
drains in ~35 ms (an 8-byte extended frame is ~130–150 bits, ~0.55 ms at 250 kbit), so
five 100 ms stalls in a row cannot happen without a real fault.

| Bus | Before | After |
|-----|--------|-------|
| Healthy | PASS, 1000/1000 | PASS, 1000/1000 |
| Nothing can acknowledge (companion on a 500 k image) | FAIL after ~94 s | **FAIL after 817 ms**: `sent 65/1000, gave up: TX queue not draining` |

65 is the 64-deep queue plus the frame held in the controller, as predicted.

## Slice 3.6 result — PASSED (2026-10-06)

The UDS lane: the companion serves UDS on ECU `0x11` beside its OBD-II server, the DUT
is the tester. **9/9** in `test_uds.py` on the first run (31.8 s), 250 kbit, 11-bit.

| Command | Result |
|---------|--------|
| `pytest . --port COM5 --companion-port COM7` | **70 passed, 8 skipped** (60.0 s), twice |
| `pytest . --port COM5 --companion-port COM7 --canable COM8` | **74 passed, 4 skipped** (72.7 s), twice — the new baseline |

74 = the 65 of slice 3.5 plus the 9 UDS tests. Without `--canable` the four CANable
tests skip as well. The UDS reset test **really restarts the companion** in every one of
these runs, and every test after it — ESP-NOW, J1939, ISO-TP, OBD-II in the next run —
passes against the rebooted board.

One more run, after the on-target unit runner (**246 Tests 0 Failures**) had been on the
DUT and the agent was flashed back: 74 passed, 4 skipped.

**Heap, checked rather than assumed.** The per-test `heap=` field reads −4444 after most
UDS tests. Free heap measured directly: 260 300 bytes before 30 further runs (six of them
4 KB downloads) and 260 212 after, so −88 bytes once, no per-run loss.

## Slice 3.7 result — PASSED (2026-10-06)

udsoncan through the CANable against the companion's UDS server: `test_uds_canable.py`
**2/2** on three runs. The companion gained an optional erase time on `FF00`, so both
boards were rebuilt and flashed.

| Command | Result |
|---------|--------|
| `pytest . --port COM5 --companion-port COM7 --canable COM8` | **76 passed, 4 skipped** (87.5 s), twice — the new baseline |

76 = the 74 of slice 3.6 plus the two new tests. The companion now really restarts twice
per run (`test_uds.py` and the udsoncan flow), and everything after both restarts passes.

## Slice 3.8 — bench back on the HIL agent (2026-10-07)

Slice 3.8 used both boards for a real reflash of example 33 at 1 Mbit (the DUT ran
example 33, the companion a 1 Mbit image). Both boards were then flashed back with the
current agent: **76 passed, 4 skipped** (87.7 s), the slice 3.7 baseline.

Two bench facts from that day: a board parked in its ROM bootloader corrupts a 1 Mbit
bus (`WIRING.md` §12.5); and the DUT's CP2102 came up with Windows error code 10
after the PC woke — unplug and replug its USB cable.

## Slice 3.9 — both sweeps PASSED (2026-10-07)

| Build | Addressing | Result (with `--canable COM8`) |
|-------|-----------|--------------------------------|
| `build` / `build_esp32s3` | 11-bit normal | **76 passed, 4 skipped** (87.8 s), twice |
| `build_nf` / `build_s3_nf` | 29-bit normal-fixed | **76 passed, 4 skipped** (87.5 s), twice |

Identical pass/skip sets, UDS included: the companion's UDS server, its real restart on
`11 01`, and udsoncan through the CANable all pass in 29-bit as in 11-bit. This closes
Phase 3 of the diagnostics ticket. Both boards were flashed back to the 11-bit images,
which are the ones the second pair of runs used.

## Security remediation, slices A-J — final regression PASSED (2026-11-03)

After the security remediation (slices A-J: J1939 TP checks, product-build guard, UDS
lockout across resets, https-only OTA, signed example 33 images, one-time secret
provisioning, example 29 and Wi-Fi settings clean-up, HIL host tooling, product
overlay and docs), everything was rebuilt from clean and run again.

| Check | Result |
|-------|--------|
| Host Unity (`python tests\host_unity\run_host_tests.py`) | UDS **100 tests, 0 failures**; OBD-II **27 tests, 0 failures** |
| Clean builds (`idf.py -C <project> [-B <dir>] clean build`), 14 build folders | all exit 0, **0 warnings** each |
| On-target Unity runner (`tests/unit_runner`, flashed to COM5) | **275 Tests 0 Failures 0 Ignored** |
| HIL, 29-bit normal-fixed (`build_nf` on COM5, `build_s3_nf` on COM7, `--isotp-addr normfixed`, CANable) | **77 passed, 4 skipped** (99.01 s) |
| HIL, 11-bit (`build` on COM5, `build_esp32s3` on COM7, CANable) | **77 passed, 4 skipped** (98.86 s) |

The 4 skips are the same in both runs: the two deferred slave drivers (`test_bus.py`),
and the two Wi-Fi tests (`test_net_mqtt.py`, `test_net_ota.py`), which say they need
`HIL_WIFI_SSID` and `HIL_WIFI_PASS`. The Wi-Fi tests were **not run**: they need the
owner's Wi-Fi settings in those two environment variables (never printed or stored in
the repository). Until they are set, the network proof stays pending.

**Commands of record.** Run `pytest` from a shell **without** the ESP-IDF environment,
with the global Python (it has `udsoncan`; the `.venv` does not):

```powershell
$py = "...\Programs\Python\Python312\python.exe"   # the global Python 3.12 (has udsoncan); replace the leading ... with your Python install folder
cd tests\hil\host
& $py -m pytest . --port COM5 --companion-port COM7 --canable COM8 -ra                       # 11-bit
& $py -m pytest . --port COM5 --companion-port COM7 --canable COM8 --isotp-addr normfixed -ra   # 29-bit
```

Each board was flashed with `idf.py -C tests\hil\agent [-B <dir> -D SDKCONFIG=...] -p COMx flash`
and only counted as flashed when the log showed "Hash of data verified" (4 times) —
the exit code alone is not enough. Both boards answered `PING` before each run (DUT on
COM5, companion on COM7). The CANable (COM8) was on the bus for both runs.

**App sizes after the clean builds** (slot size 0x180000 = 1572864 bytes):

| Build folder | App size (bytes) | Headroom (bytes) |
|--------------|-----------------:|-----------------:|
| `tests/unit_runner/build` | 1293632 | 279232 |
| `tests/hil/agent/build` | 1261552 | 311312 |
| `tests/hil/agent/build_esp32s3` | 1246000 | 326864 |
| `tests/hil/agent/build_nf` | 1261648 | 311216 |
| `tests/hil/agent/build_s3_nf` | 1246112 | 326752 |
| `examples/29_edge_gateway/build` | 1136992 | 435872 |
| `examples/12_wifi_sta/build` | 777872 | 794992 |
| `examples/33_uds_server/build_ota1m` (and `_b`) | 393204 | 1179660 |
| `examples/28_secure_ota/build` | 327668 | 1245196 |
| `examples/33_uds_server/build_bench` | 292880 | 1279984 |
| `examples/25_ota/build` | 228800 | 1344064 |
| `examples/26_ota_http/build` | 224512 | 1348352 |
| `examples/23_mqtt/build` | 177920 | 1394944 |

The HIL agent is about 72 KB bigger than when it was measured on 2026-10-08 (1189056
bytes). Its headroom was then about 375 KiB; it is now about 304 KiB.

**One side effect to know about.** The first clean build of `build_nf` and `build_s3_nf`
since the Kconfig changes of slices B to J rewrote `tests/hil/agent/sdkconfig.sweep_nf`
and `sdkconfig.sweep_nf.esp32s3` (new options added by the build system). Those are the
only two of 106 project `sdkconfig*` files whose hash changed. The other 104 are
unchanged. The new files have the same settings as the project `sdkconfig` and
`sdkconfig.esp32s3`, apart from the 29-bit addressing option. (There is no copy of the
old files, so the exact old-versus-new difference was not recorded.)

At the end both boards were flashed back to the 11-bit images (`build` on COM5 as DUT,
`build_esp32s3` on COM7 as companion) and both answered `PING`.

## Sites this baseline is watching

Slice 1.0 makes three currently-degenerate delays real (ticket §3.5). Each has a
test that covers it:

| Site | Today at HZ=100 | After slice 1.0 | Covered by |
|------|-----------------|-----------------|------------|
| `hil_can.c:253` — `bus_down()` TX drain | `vTaskDelay(0)`, 60 bare yields | 60 × 5 ms = 300 ms real budget | every two-board `test_can.py` case |
| `hil_can.c:380` — TP hand-pump | non-blocking poll, 6 s spin | real 5 ms block, loop at 200 Hz | `test_can_tp_bam`, `test_can_tp_cmdt` |
| `hil_sys.c:38` — event dispatch wait | bare yield | 20 × 5 ms = 100 ms budget | `test_sys_event` |

`test_can_tp_bam` is the one to read first if the post-change run goes red —
ticket §3.9 computes its margin as 3× (250 B = 36 packets × 50 ms ≈ 1.8 s against
a 6 s budget), so it should hold, but it is the only case where slice 1.0 makes a
loop slower rather than faster.
