# Host Unity runner

Runs a component's Unity suite on the PC instead of the board — seconds instead of a
flash, and no board needed at all. It uses the **real** Unity from ESP-IDF, so a test
that passes here passes the same assertions as on the target.

```powershell
python tests/host_unity/run_host_tests.py              # every suite
python tests/host_unity/run_host_tests.py e32p_uds     # one suite
```

Needs `gcc` on PATH (MinGW-w64 on Windows) and ESP-IDF installed, for Unity's source
files (`IDF_PATH`, or the default `C:/Espressif/frameworks/esp-idf-v5.3.1`). Builds go
to `build/`. The exit code is non-zero if anything fails to build, a suite crashes, or
any test fails.

## What can run here

Only **pure** code: logic with no peripheral, task or clock behind it — protocol
engines, parsers, state machines, stores that sit behind a small interface. That is
why the diagnostics components are built the way they are: the OBD-II and UDS service
layers take requests and time as arguments, so they run here unchanged.

Anything that touches hardware stays on the target. **The target's unit runner
(`tests/unit_runner`) is still the source of truth**; this is the fast loop in front of
it.

| Suite | Host-built sources |
|-------|-------------------|
| `e32p_obd2` | `e32p_obd2.c`, `e32p_obd2_dtc.c` (the store runs on an in-memory NVS) |
| `e32p_uds` | `e32p_uds.c`, `e32p_uds_security.c`, `e32p_uds_transfer.c`, `e32p_uds_dtc.c`, `e32p_uds_routine.c` |

## How it works

- **`host_testcase.h`** gives the test file the same `TEST_CASE` macro ESP-IDF does.
  It is force-included at compile time, so the test source is not touched.
- **`host_main.c`** runs every registered case with Unity, in source order.
- **`shim/`** holds stand-ins for the only two platform headers pure code includes:
  `e32p_core.h` (error codes and check macros, without logging) and `e32p_nvs.h`
  (an in-memory key/value store).

## Adding a suite

Add one line to `SUITES` in `run_host_tests.py`: the component name and the sources
that build without ESP-IDF. If a source needs another platform header, add the
smallest stand-in that works to `shim/` — and if it needs a real peripheral, it does
not belong here.

## Trusting a green result

A runner that cannot fail proves nothing. When this one was built, a deliberately
failing case was run through it: Unity reported the failure with its message, carried
on to the next case, and the exit code was non-zero.
