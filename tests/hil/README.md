# HIL — Hardware-in-the-Loop test framework

An integration + system test rig for the platform, layered **above** the headless
unit tests (`tests/unit_runner`). It drives real hardware across two boards and a
PC orchestrator, exercising whole feature stacks bottom-to-top (e.g. CAN driver →
J1939 → J1939Tp), plus negative, boundary, stress, and parallel scenarios.

> **Status:** the two-board rig (2× ESP32, CAN transceivers, BMP280) is now on the
> bench, so the two-board and sensor tests run for real — see
> [`WIRING.md`](WIRING.md) for how to connect each one. Single-board tests run with
> just `--port`.

## Test layers (where this fits)

| Layer | Where | Scope |
|-------|-------|-------|
| Unit | `tests/unit_runner` (Unity) | per-module logic, headless, one board |
| **Integration / system** | **`tests/hil` (this)** | full feature stacks over real hardware, two boards + PC |

## Topology

```
        PC  (pytest-embedded orchestrator)
        │ USB-serial (control)      │ USB-serial (READ-ONLY)
        ▼                           ▼
   ┌─────────┐   DUT bus under test   ┌───────────┐
   │ Board A │◄═══ CAN / SPI / I2C ═══►│  Board B  │
   │  DUT    │     UART / ESP-NOW      │ COMPANION │
   └─────────┘                        └───────────┘
```

- **Control plane is USB-serial, decoupled from the buses under test.** When CAN
  (or any bus) is being stress-tested, the orchestration link is unaffected. Many
  tests need no transceiver at all.
- **Board A = DUT.** The PC sends it commands and reads its self-reported verdict.
- **Board B = COMPANION.** Runs autonomously as the peer for the active feature.
  The PC reads its serial **read-only**, purely to *corroborate* A's result — two
  independent sources of truth, minimal coupling.

## One codebase, per-target builds

The agent is **one codebase** that builds for each board's chip (`esp32`,
`esp32s3`, …). Pins are resolved per target in [`agent/main/hil_pins.h`](agent/main/hil_pins.h),
and chip-specific modules are gated per build (the S3 has no DAC, a different
touch peripheral, and a different ADC-calibration scheme, so those three tests
report "skipped" there). Because two boards join by wire/bus, a **heterogeneous
pair** (e.g. WROOM ↔ S3) works with different GPIOs on each end — see
[`WIRING.md`](WIRING.md) for the per-board pin table and the two-target
build/flash commands.

## One binary per target, two roles

Both boards run the **same** agent firmware (built for their chip). Each reads its
role from **NVS** (`namespace "hil", key "role"`): `DUT` or `COMPANION`. Provision
once per board:

```
@CMD SETROLE DUT          # on board A (persists across reboots/reflashes)
@CMD SETROLE COMPANION    # on board B
```

Role → behaviour mapping (real-world honest, no per-test manipulation):

| Feature | DUT (A) | COMPANION (B) |
|---------|---------|---------------|
| SPI | master (drives SCLK/CS) | slave (`e32p_spi_slave`, deferred) |
| I²C | master (initiates, drives SCL) | slave with address (`e32p_i2c_slave`, deferred) |
| CAN / J1939 | peer, SA 0x80 | peer, SA 0x21 (no master/slave; addresses/claiming) |
| UART | peer | peer |
| ESP-NOW | peer (its MAC) | peer (its MAC) |

Roles are a **provisioned identity**, not a per-test command — the PC never
puppeteers B's behaviour.

## Serial protocol (control plane)

Line-based ASCII, `\r\n`-terminated. Protocol lines are prefixed so the PC can
separate them from `ESP_LOGx` output on the same UART.

| Line | Direction | Meaning |
|------|-----------|---------|
| `@CMD <verb> [args]` | PC → board | a command |
| `@RSP <verb> <status> [detail]` | board → PC | the response |
| `@EVT <name> [k=v ...]` | board → PC | async event (e.g. `READY`) |
| `@ERR <message>` | board → PC | malformed / unknown command |

Skeleton commands:

| Command | Response |
|---------|----------|
| `@CMD PING` | `@RSP PING OK` |
| `@CMD ROLE` | `@RSP ROLE <DUT\|COMPANION\|UNSET>` |
| `@CMD SETROLE <r>` | `@RSP SETROLE OK` (persists to NVS) |
| `@CMD SYSINFO` | `@RSP SYSINFO chip=ESP32 cores=2 heap=<n> uptime=<ms> reset=<r>` |
| `@CMD LIST` | `@RSP LIST <id1>,<id2>,...` |
| `@CMD RUN <id>` | `@RSP RUN <id> PASS heap=<±n> t=<ms>ms detail=...` / `... FAIL detail=...` |

Each `RUN` measures **heap delta** (leak detection) and **duration**, so every
test carries diagnostics, not just PASS/FAIL. On boot the agent emits
`@EVT READY role=<r>`.

## Layout

```
tests/hil/
├── README.md          # this document (architecture + protocol + how-to-run)
├── TEST_MATRIX.md      # the coverage taxonomy (feature × layer × category)
├── agent/              # the ESP-IDF firmware — ONE binary for both boards
│   ├── main/
│   ├── CMakeLists.txt
│   └── sdkconfig.defaults
└── host/               # the PC harness: pytest + raw pyserial
    ├── conftest.py
    ├── test_*.py        # one file per feature group
    ├── console_log.py   # watch a board's console without resetting it
    ├── pytest.ini
    └── requirements.txt
```

## How to run

**pytest does not flash** — flashing stays out of the harness. Both boards enter the
bootloader on their own; the WROOM needs its 10 µF capacitor from EN to GND for that
(`WIRING.md` §13), and without it every flash needs BOOT held down. You flash the agent once; pytest talks to
the already-flashed board over **raw pyserial** (the `agent` fixture in
`conftest.py`), opening the port with `dtr=0/rts=0`. That last part matters: this
board goes unresponsive if RTS is asserted while DTR is low, and pytest-embedded's
serial service left the port in exactly that state — so the harness drives the
serial lines itself. (`--port` is still borrowed from the installed
pytest-embedded plugin; `serial_probe.py` is the tool that diagnosed the line
state.)

**1. Flash the agent** (the VS Code task or `idf.py` — no buttons). The agent is
already built under `tests/hil/agent/build`:

```powershell
idf.py -C tests/hil/agent -p COM5 flash monitor
```

**2. Run the smoke tests** (single board, no wiring):

```powershell
cd tests/hil/host
python -m venv .venv ; .venv\Scripts\activate ; pip install -r requirements.txt
pytest --port COM5 .
```

`requirements.txt` is pinned (`==`) to the versions the suite is run with. The
baseline runs use the **global** Python 3.12 (`...\Programs\Python\Python312\python.exe
-m pytest . ...`), which has every package in that file, `udsoncan` included. A venv
built from `requirements.txt` has the same set. Run from a shell without the ESP-IDF
environment active so the IDF Python does not shadow it.

The `--port` selects the board; nothing is flashed. Close any serial monitor on
that port first (only one program can own the COM port).

> Always give pytest a target — an explicit test file, or a trailing **`.`** when
> you don't. Without one it won't load this folder's `conftest.py` (so custom
> options like `--companion-port` are rejected) and may wander into `.venv`.

**Two boards:** flash both (`SETROLE DUT` on A, `SETROLE COMPANION` on B — the role is
kept in NVS), then give the companion its own option. Add the CANable lane with
`--canable` once Cangaroo is closed:

```powershell
pytest . --port COM5 --companion-port COM7
pytest . --port COM5 --companion-port COM7 --canable COM8
```

**Watching a board without resetting it:** opening a port normally pulses DTR and RTS,
which resets these DevKits. `console_log.py` opens with both lines already low, so the
board keeps running — use it to watch for a crash. It timestamps every line and, at the
end, lists panics, resets and backtraces with the `addr2line` command to decode them:

```powershell
python console_log.py COM7 --seconds 60 --out companion.log --elf ../agent/build_esp32s3/hil_agent.elf
```

**Network tests (MQTT + OTA round-trips):** these automate the manual Mosquitto /
`python -m http.server` procedures — the PC runs the broker and the file server,
and the agent joins Wi-Fi and connects back. They skip unless Wi-Fi creds are set in
the environment. The SSID comes from `HIL_WIFI_SSID` (or `--wifi-ssid`); the password
comes **only** from `HIL_WIFI_PASS` — there is no password command-line option, so it
never lands in shell history or the process list. An empty/unset password means an
open network:

```powershell
$env:HIL_WIFI_SSID = "MyNet" ; $env:HIL_WIFI_PASS = "secret"
pytest --port COM5 test_net_mqtt.py test_net_ota.py
```

The VS Code task "hil tests 1 board" sets both variables from its prompts. Do not put
the values in a file in this repo, and rotate them if they ever were.

Do not run the network tests with `-l` / `--showlocals`: the password would show in a
failure traceback.

The broker and the OTA file server bind to this PC's **LAN IP only** (the address
the agent is told to connect to), not `0.0.0.0`; the PC-side MQTT client connects to
that same address. They are therefore not reachable from other interfaces, but they
are still open to every device on that LAN while a test runs (the broker allows
anonymous clients) — run them on a trusted bench network.

Needs Mosquitto installed, `paho-mqtt` (in `requirements.txt`), and inbound TCP
**1883** (broker) + **8080** (`--http-port`, OTA server) + **8443** (`--https-port`,
https OTA server) allowed through the PC firewall. `test_net_ota` reboots the board
(it pulls the agent's own image and asserts the OTA slot flips). The agent must be
built with `CONFIG_ESP_HTTPS_OTA_ALLOW_HTTP=y` (already in its `sdkconfig.defaults`) —
that's a **testing-only** insecure shortcut; production OTA uses https + a verified
certificate (see `components/e32p_ota/README.md`).

`test_net_ota_https` does the same pull over https and checks the main task's stack
high-water mark after it (at least 1024 bytes free). The agent trusts a test-only
**HIL bench CA** through its CA bundle. Make the CA once, then rebuild and flash the
agent:

```powershell
cd tests/hil/host
python -I tools/make_bench_ca.py   # writes agent/bench_ca/hil_bench_ca.der + .bench_ca/hil_bench_ca.key
```

The `.der` is the public cert and belongs in the repo. The `.key` stays on this PC
(git-ignored); without it the test skips. Each run signs a new server cert for this
PC's LAN IP and deletes it afterwards. Add `-rP` to see the high-water mark.

## Building out from here

Add test cases to the agent's registry and matching pytest cases per
`TEST_MATRIX.md`. Two-board feature groups (CAN/J1939 first) come after the
framework; SPI/I²C additionally need the deferred `e32p_spi_slave` /
`e32p_i2c_slave` companion drivers. See `TEST_MATRIX.md` for the plan.
