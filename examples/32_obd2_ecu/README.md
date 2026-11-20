# 32_obd2_ecu — an OBD-II ECU a scan tool can talk to

One board plays an engine ECU: live coolant temperature, RPM and speed, a VIN,
and a trouble code that appears the first time the simulated engine overheats.
A second board plays the scan tool and prints what it reads. Or leave the second
board out and ask the ECU from a PC through a CANable.

Built on `e32p_obd2` (OBD-II services) over `e32p_isotp` (ISO 15765-2).

## What you see

The tester prints one round a second, read over real CAN at 500 kbit:

```
--- OBD-II round ---
  supported: 08 18 00 10
  coolant  : 56 C
  rpm      : 1124
  speed    : 38 km/h
  vin      : E32PDEMO000000001
  codes    : P0217
```

- **supported** is the PID bitmap. It is worked out from the ECU's PID table, so
  it can never advertise something the ECU does not answer.
- **coolant, rpm, speed** drift on a slow sine (about 31 s per cycle).
- **vin** is 20 bytes on the wire, so it arrives in several CAN frames and is put
  back together by ISO-TP.
- **codes**: P0217 (engine over-temperature) is stored the first time coolant
  passes 92 °C. It lives in NVS, so it is still there after a reset or a reflash,
  until something sends mode `0x04` to clear it.

## Wiring

Two ESP32 boards, each with an SN65HVD230 (or similar) CAN transceiver, CANH to
CANH, CANL to CANL, and **one 120 Ω terminator at each end** of the bus. Pins
default to the platform bench map: WROOM TX 21 / RX 22, S3 TX 4 / RX 5.

A missing terminator does not just lose frames: on this bench it put the
receiving board into a crash loop. If nothing answers, check both terminators
first.

## Build and flash

Each role is a Kconfig choice, picked with an overlay file. Give each role its
own build directory and `sdkconfig`, or a second build silently keeps the first
one's role.

```powershell
# ECU on the WROOM (COM5)
idf.py -C examples\32_obd2_ecu -B build_ecu -D SDKCONFIG=sdkconfig.ecu -D IDF_TARGET=esp32 `
       -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.role_ecu" -p COM5 flash monitor

# Tester on the S3 (COM7)
idf.py -C examples\32_obd2_ecu -B build_tester_s3 -D SDKCONFIG=sdkconfig.tester.esp32s3 -D IDF_TARGET=esp32s3 `
       -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.role_tester" -p COM7 flash monitor
```

## Asking from a PC

With only the ECU on the bus, a CANable at 500 kbit and `can-isotp` do the
tester's job:

```python
import can, isotp
bus = can.interface.Bus(interface="slcan", channel="COM8", bitrate=500000)
addr = isotp.Address(isotp.AddressingMode.Normal_11bits, txid=0x7E0, rxid=0x7E8)
stack = isotp.NotifierBasedCanStack(bus=bus, notifier=can.Notifier(bus, []), address=addr)
stack.start()
stack.send(b"\x01\x0C")          # engine RPM
# ... stack.recv() returns 41 0C A B; RPM = (256*A + B) / 4
```

## Notes

| Item | Detail |
|---|---|
| **Bitrate** | 500 kbit, the usual OBD-II rate. Every node must match, including a CANable. |
| **Addressing** | 11-bit by default: requests on `0x7E0` (physical) and `0x7DF` (broadcast), answers on `0x7E8`. Switch to 29-bit in `e32p_isotp`'s Kconfig. |
| **A second ECU** | Set `EX32_ECU_ADDR` to 17 for `0x7E1` / `0x7E9`, and so on. |
| **Partition table** | This example uses the default single-app table. Flashing it over a board that runs the platform's dual-OTA table leaves the other partitions alone; flash the other project again to go back. |
| **Tester errors** | The tester prints the real error, e.g. `no reply (ESP_ERR_TIMEOUT)`, which tells "sent but nothing came back" from "could not send". |
