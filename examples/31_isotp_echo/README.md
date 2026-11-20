# 31_isotp_echo — ISO-TP send, reassemble, echo

Proves `e32p_isotp` end to end. Node A sends a payload that doesn't fit in one CAN
frame; node B reassembles it and sends it back; node A checks every byte. By
default the payload is 100 bytes, so it goes out as a First Frame, a Flow Control,
and 14 Consecutive Frames.

ISO-TP (ISO 15765-2) is the transport OBD-II and UDS run on. This example is the
foundation the later OBD-II and UDS examples build on.

## Three roles

Pick one in `menuconfig` → **31_isotp_echo → Node role**.

| Role | Hardware | What it shows |
|------|----------|---------------|
| **Loopback** (default) | One board, nothing wired | Both ends run in one app over an in-process queue. The quickest check that the engine works. |
| **Node A** — tester | Board + CAN transceiver | Sends the payload, waits for the echo, checks it byte for byte. |
| **Node B** — ECU | Board + CAN transceiver | Reassembles each request and echoes it back. |

## Build and flash

Each role uses its own build folder and its own `sdkconfig` file. That matters: a
shared `sdkconfig` would keep the old role's settings, because an existing
`sdkconfig` always wins over `sdkconfig.defaults`.

Set up the environment first:

```powershell
. C:\Espressif\Initialize-Idf.ps1 -IdfId esp-idf-ab7213b7273352b64422b1f400ff27a0
$EX = "C:\Users\aleks\Work\esp32_platform\examples\31_isotp_echo"
```

**Loopback** — any single board (here the WROOM on COM5):

```powershell
idf.py -C $EX -p COM5 flash monitor
```

**Node A** on the WROOM (COM5):

```powershell
idf.py -C $EX -B $EX\build_a -D SDKCONFIG=sdkconfig.role_a -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.role_a" -p COM5 flash monitor
```

**Node B** on the ESP32-S3 (COM7) — set the target once, then flash:

```powershell
idf.py -C $EX -B $EX\build_b_s3 -D SDKCONFIG=sdkconfig.role_b.esp32s3 -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.role_b" set-target esp32s3
idf.py -C $EX -B $EX\build_b_s3 -D SDKCONFIG=sdkconfig.role_b.esp32s3 -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.role_b" -p COM7 flash monitor
```

Exit the monitor with **Ctrl+]**.

## Wiring (nodes A and B)

Use the CAN wiring from `tests/hil/WIRING.md` §7.1. Each board wires its own TWAI
pins to its own transceiver; the two transceivers share CANH and CANL.

| | WROOM | ESP32-S3 |
|---|---|---|
| TWAI TX → transceiver D | GPIO 21 | GPIO 4 |
| TWAI RX ← transceiver R | GPIO 22 | GPIO 5 |

- Put a **120 Ω resistor at each end** of the bus, and connect the two grounds.
- On a VP230 / SN65HVD230, tie the **RS pin to GND** for high-speed mode.
- The bus runs at **500 kbit/s**. Every node must match — including a CANable
  used as a sniffer. A node at the wrong rate floods the bus with error frames.
- Start node B first. Node A recovers on its own if it finds the bus with no one
  to acknowledge it, but it will log `bus-off` until B is up.

## What you should see

The shape of each line (values here are placeholders, not measured output):

```
NODE A (tester): 100-byte payload, 500000 bit/s, normal 11-bit (0x7E0 / 0x7E8)
PASS #0 len=100 t=<ms> (pass 1 / fail 0)
PASS #1 len=100 t=<ms> (pass 2 / fail 0)
```

```
NODE B (ECU): echoing, 500000 bit/s, normal 11-bit (0x7E0 / 0x7E8)
echoed 1 payload(s), last 100 bytes
```

Loopback prints the same `PASS` lines as node A.

## Settings

All under `menuconfig`.

| Setting | Where | Default |
|---|---|---|
| Node role | 31_isotp_echo | Loopback |
| Payload length | 31_isotp_echo | 100 bytes (1–4095) |
| CAN TX / RX pins | 31_isotp_echo | 21 / 22 on WROOM, 4 / 5 on S3 |
| Bitrate | e32p: isotp | 500 kbit/s in this example |
| Addressing | e32p: isotp | Normal 11-bit |

Payloads of 1–7 bytes go out as a single frame. Anything longer uses segmentation.
Switching addressing to normal-fixed 29-bit moves the IDs to `0x18DA10F1` and
`0x18DAF110`.

## If it fails

| Log | Likely cause |
|---|---|
| `FAIL #n: timeout - is node B running…` | Node B not flashed or not powered, bus not terminated, or the two boards at different bitrates. |
| `bus-off … recovering` | Node A transmitted with nobody to acknowledge it. Start node B. |
| `FAIL #n: echo … bytes differ` | Frames were lost or reordered on the way. Check the wiring and termination first. |

## Bus capture

Captured with a CANable on the bus at 500 kbit/s while node A and node B were
running. It shows one full round: A's 100-byte request, then B's echo.

![ISO-TP exchange at 500 kbit/s](docs/isotp_capture_500k.png)

| Frames | ID | Data | What it is |
|---|---|---|---|
| 1 | `0x7E0` | `10 64 78 7F 86 8D 94 9B` | First Frame from A. Total length `0x064` = 100 bytes, then the first 6 data bytes. |
| 2 | `0x7E8` | `30 08 01 AA AA AA AA AA` | Flow Control from B: clear to send, block size 8, 1 ms minimum gap, padded with `0xAA`. |
| 3–10 | `0x7E0` | `21 …` to `28 …` | Consecutive Frames 1–8, one full block. |
| 11 | `0x7E8` | `30 08 01 AA …` | B asks for the next block. |
| 12–17 | `0x7E0` | `29 …` to `2E 1F 26 2D AA AA AA AA` | Consecutive Frames 9–14. The last one carries the final 3 bytes plus 4 bytes of padding. |
| 18–34 | `0x7E8` / `0x7E0` | same sequence, IDs swapped | B's echo, byte for byte the same as the request. |

What the capture confirms:

- **The length adds up.** 6 bytes in the First Frame, 13 full Consecutive Frames
  of 7, and 3 in the last one make 100.
- **Flow control works in both directions.** There is exactly one Flow Control
  per block of 8, and none after the final short block.
- **Sequence numbers are right.** They start at 1 after the First Frame and count
  up with no gaps.
- **The data is the data the firmware sent.** Byte *i* is `7i + seed`. The first
  byte `0x78` gives seed 120, and byte 99 is `0x2D`, the last byte on the wire.
- **Padding is `0xAA`** on both Flow Control frames and on the last
  Consecutive Frame.

What it does not show:

- **Timing.** The time column only ever reads `0.000` or `0.015`, which is the
  PC's timer resolution of about 15 ms. The 1 ms gap between frames is too small
  to see here. Use a logic analyzer for timing.
- **The sequence number wrapping.** 100 bytes needs only 14 Consecutive Frames,
  so the count stops at `2E`. Set the payload to 112 bytes or more to see `2F`
  wrap around to `20`.

## Files

| File | Purpose |
|---|---|
| `main/main.c` | All three roles |
| `main/Kconfig.projbuild` | Role, payload length, CAN pins |
| `sdkconfig.defaults` | Enables TWAI + ISO-TP, 500 kbit, 1 ms tick, deeper TWAI queues |
| `sdkconfig.defaults.role_a` / `.role_b` | One-line overlays that pick the role |
| `sdkconfig.defaults.250k` | Overlay that drops the bus to 250 kbit/s |
| `docs/isotp_capture_500k.png` | The bus capture shown above |
