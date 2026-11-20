# _template — copy-source for new e32p modules

This folder is not a component. It sits at the repo root (not under
`components/`) so ESP-IDF never scans it — that keeps a stray
`CONFIG_E32P_FOO_ENABLE` out of every consuming project's menuconfig.

## How to create a new module

1. Copy the entire `_template/` folder into `components/` and rename it to
   `components/e32p_<name>/` (for example `components/e32p_button/`).
2. Rename `foo` to `<name>` everywhere: file names, the handle typedef, the
   config/status structs, all function names, the `TAG` string, the Kconfig
   menu title, and `CONFIG_E32P_FOO_ENABLE` -> `CONFIG_E32P_<NAME>_ENABLE`.
3. Fill in the real config fields, driver calls, and the pin/gotcha table
   below.
4. Update `_template/idf_component.yml` (copied in as
   `components/e32p_<name>/idf_component.yml`) only if the module needs a
   managed dependency from the Component Registry; otherwise delete it.

## Public API (what every module exposes)

| Function                 | Purpose                                   |
|---------------------------|--------------------------------------------|
| `e32p_foo_init`           | Allocate and configure an instance.        |
| `e32p_foo_deinit`         | Release an instance (NULL-safe; do not reuse the handle). |
| `e32p_foo_start`          | Start operation.                           |
| `e32p_foo_stop`           | Stop operation.                            |
| `e32p_foo_get_status`     | Read a runtime status snapshot.            |

| Type                     | Purpose                                   |
|---------------------------|--------------------------------------------|
| `e32p_foo_handle_t`       | Opaque instance handle.                    |
| `e32p_foo_config_t`       | Caller-owned configuration struct.         |
| `E32P_FOO_CONFIG_DEFAULT()` | Sane default configuration.              |
| `e32p_foo_status_t`       | Runtime status snapshot.                   |

## Pin / gotcha table

Fill this in for the real module before it ships.

| Item | Detail |
|------|--------|
|      |        |
|      |        |
|      |        |

(Things to consider: reserved SPI-flash pins GPIO6-11, strapping pins
GPIO0/2/12/15, ADC2-vs-Wi-Fi conflicts, any peripheral that is single-instance
on this chip.)

## Tests

`test/test_e32p_foo.c` + `test/CMakeLists.txt` register the module's Unity
cases as a component. The tests do **not** build with the module on their own —
a dedicated test-runner project lists the module in `TEST_COMPONENTS` and builds
them on-target. That runner arrives with the platform's example/test milestone;
until then the test files are copy-source that stays compilable.

## Chip-specific modules

If the module wraps a peripheral that not every target has, add
`depends on SOC_<FEATURE>_SUPPORTED` to its Kconfig entry so the option
disappears on targets that lack it, instead of failing at build time.
