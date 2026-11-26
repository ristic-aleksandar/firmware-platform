# ESP32 Platform

A modular ESP32 firmware platform for **ESP-IDF v5.3.1**. Every hardware
capability and protocol is a small, self-contained `e32p_*` component you can
**cherry-pick** — enable only what a project needs and build your application on
top. Thin facades, a uniform API and error model, and an on-target test for
every module.

Target: ESP32-WROOM-32 (portable-ready via `SOC_*` guards).

## Status: a learning and portfolio project, not for production

This is **not production-ready** and has not been used in a vehicle or any
product. It has only been tested by its own test suites on a small bench (two ESP32
boards and a CAN adapter), and the protocol work is not as deep or complete as a
shipping product would need. Do not connect it to a real vehicle or any safety- or
security-relevant system. The UDS, OBD-II and J1939 parts are written against the
standards as the author could access them, and they have not been through an
official conformance test. See [Security](#security) for the audit results and open
items.

## Flagship: industrial edge gateway

The modules aren't just a parts bin — they compose into products. The
[**`29_edge_gateway`**](examples/29_edge_gateway/README.md) example is a complete
one: it reads a J1939/CAN field bus, normalizes the signals, and streams them to
an MQTT cloud — while being commissioned over BLE, controlled from the cloud, and
updated over the air, with a local display and a watchdog. One device, ~14
modules, a real product lifecycle: **provision → acquire → report → control →
update**.

## Start here

| I want to… | Go to |
|------------|-------|
| Understand the whole system | [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) |
| See the modules working together as a product | [`examples/29_edge_gateway`](examples/29_edge_gateway/README.md) |
| Use a module (API/usage) | that module's `components/e32p_<x>/README.md` |
| Understand a module's internals | that module's `components/e32p_<x>/DESIGN.md` |
| See what's tested / verify on hardware | [`docs/TEST_PLAN.md`](docs/TEST_PLAN.md) |
| Write docs for a new module | copy [`docs/MODULE_TEMPLATE.md`](docs/MODULE_TEMPLATE.md) |

## Modules

**Foundation:** [`e32p_core`](components/e32p_core) ·
[`e32p_event`](components/e32p_event) · [`e32p_nvs`](components/e32p_nvs) ·
[`e32p_sysinfo`](components/e32p_sysinfo) · [`e32p_wdt`](components/e32p_wdt) ·
[`e32p_pm`](components/e32p_pm) · [`e32p_ota`](components/e32p_ota)

**GPIO / analog / timers:** [`e32p_gpio`](components/e32p_gpio) ·
[`e32p_ledc`](components/e32p_ledc) · [`e32p_adc`](components/e32p_adc) ·
[`e32p_dac`](components/e32p_dac) · [`e32p_touch`](components/e32p_touch) ·
[`e32p_pcnt`](components/e32p_pcnt) · [`e32p_gptimer`](components/e32p_gptimer)

**Buses:** [`e32p_uart`](components/e32p_uart) ·
[`e32p_i2c`](components/e32p_i2c) · [`e32p_spi`](components/e32p_spi) ·
[`e32p_twai`](components/e32p_twai) · [`e32p_i2s`](components/e32p_i2s) ·
[`e32p_ws2812`](components/e32p_ws2812)

**Wireless:** [`e32p_wifi`](components/e32p_wifi) ·
[`e32p_espnow`](components/e32p_espnow) · [`e32p_mqtt`](components/e32p_mqtt) ·
[`e32p_ble`](components/e32p_ble)

**CAN / J1939 / diagnostics:** [`e32p_j1939`](components/e32p_j1939) ·
[`e32p_j1939tp`](components/e32p_j1939tp) ·
[`e32p_j1939_acl`](components/e32p_j1939_acl) ·
[`e32p_isotp`](components/e32p_isotp) ·
[`e32p_obd2`](components/e32p_obd2) ·
[`e32p_uds`](components/e32p_uds)

**Serial / industrial:** [`e32p_modbus`](components/e32p_modbus)

**Device drivers:** [`e32p_bmp280`](components/e32p_bmp280) ·
[`e32p_tm1637`](components/e32p_tm1637)

## Cherry-pick

Enable the modules an application needs; Kconfig `select` pulls their
dependencies automatically. In the app's `sdkconfig.defaults`:

```
CONFIG_E32P_J1939TP_ENABLE=y   # pulls in e32p_j1939 -> e32p_twai -> the driver
```

Each component's sources compile in only when its symbol is set, so unused
modules cost nothing.

## Build & flash

Each `examples/<n>_<name>/` is a standalone ESP-IDF project pointing at
`../../components`:

```powershell
# Activate ESP-IDF v5.3.1, then:
idf.py -C examples/17_j1939_send -p COM5 build flash monitor
```

Or use the **VS Code tasks** in `main.code-workspace` (pick project / COM / chip).
Exit the serial monitor with **Ctrl+]**.

## Test

`tests/unit_runner` builds and runs every module's Unity suite on-target; protocol
logic (J1939 codec, Transport Protocol state machines) is proven headless with no
bus. See [`docs/TEST_PLAN.md`](docs/TEST_PLAN.md).

Last full run (bench: ESP32-WROOM-32 DUT, ESP32-S3 companion, CAN adapter):

| Suite | Result |
|---|---|
| Hardware-in-the-loop (pytest, two boards on a live CAN bus) | 79 passed, 2 skipped (deferred drivers) |
| On-target Unity runner | 275 passed, 0 failed |
| Host Unity: UDS / OBD-II | 100 passed / 27 passed, 0 failed |

These counts only show that the code does what its own tests expect. They are not
a conformance or certification result.

## License

[MIT](LICENSE). The software is provided "as is", without warranty of any kind.

## Layout

```
components/   e32p_* modules (include / src / test / README / DESIGN)
examples/     one runnable demo per module (00_…), plus the 29_edge_gateway flagship
tests/        aggregate on-target Unity runner
docs/         ARCHITECTURE · MODULE_TEMPLATE · TEST_PLAN
partitions.csv  4 MB dual-OTA
```
