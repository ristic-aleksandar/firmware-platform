# Unit test runner

Builds and runs every module's Unity test suite on-target. It enables all
`e32p_*` modules and pulls in their `test/` directories via `TEST_COMPONENTS`,
then `unity_run_all_tests()` runs them all and prints a summary — no input
needed, no external hardware required.

## Build & flash

```powershell
. C:\Espressif\Initialize-Idf.ps1 -IdfId esp-idf-ab7213b7273352b64422b1f400ff27a0
# From the repository root
idf.py -C tests\unit_runner set-target esp32
idf.py -C tests\unit_runner -p COM5 build flash monitor
```

`TEST_COMPONENTS` is set in the project `CMakeLists.txt`, so a plain
`idf.py build` includes the suites. (Equivalently: `idf.py -T e32p_gpio -T e32p_i2c ... build`.)

## Expected output

Each `TEST_CASE` prints a line, ending with a summary:

```
I (…) unit_runner: running all e32p unit tests...
...
<n> Tests 0 Failures 0 Ignored
```

A non-zero failure count names the failing case and file:line. Some cases need
hardware to be *meaningful* (they still pass headless) — e.g. the SPI transfer
runs but can only prove a byte-match with a MOSI→MISO jumper; see the module
`test/` files and `docs/TEST_PLAN.md`.

## Adding a module's tests

When a new `e32p_*` module gains a `test/` dir:
1. add a `test/CMakeLists.txt` that `idf_component_register`s the suite
   (`REQUIRES unity e32p_<name>`, `WHOLE_ARCHIVE`) — without it the configure
   fails with *"does not contain a component"*;
2. add the module to the `TEST_COMPONENTS` list in `CMakeLists.txt`; and
3. add `CONFIG_E32P_<NAME>_ENABLE=y` to `sdkconfig.defaults`.

Then **`fullclean` and rebuild** before flashing:

```powershell
idf.py -C <runner> fullclean
idf.py -C <runner> set-target esp32
idf.py -C <runner> build
```

`TEST_COMPONENTS` is a **`CACHE STRING`** — CMake writes it only at the *first*
configure and ignores later edits to the `set(... CACHE ...)` line until the
cache is cleared. A plain `clean build` keeps `CMakeCache.txt`, so it silently
keeps running the *old* suite list (this is exactly how espnow/twai/i2s/ws2812
went missing once). Only `fullclean` (or deleting `build/`) forces a fresh read
of the list. `sdkconfig.defaults` is a separate concern — it only seeds a fresh
`sdkconfig`, so also delete `sdkconfig` if a new module's enable isn't taking.

## Running a subset

Change `unity_run_all_tests()` to `unity_run_menu()` in `main/main.c` for an
interactive menu (run by name or `[tag]`), or edit the `TEST_COMPONENTS` list in
`CMakeLists.txt` to build fewer suites.

## Faster loop for pure code

Suites whose code needs no hardware (the OBD-II and UDS service layers) also run on
the PC in seconds with `python tests/host_unity/run_host_tests.py` — the real Unity,
no board. This runner stays the source of truth; see `tests/host_unity/README.md`.
