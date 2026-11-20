# 33_uds_server

A UDS (ISO 14229-1) ECU on the CAN bus, with a reflash path. A PC tester talks to it
through a CANable — sessions, data identifiers, security access, trouble codes,
routines, and a firmware download — using [udsoncan](https://github.com/pylessard/python-udsoncan),
a UDS client we did not write.

Built on [`e32p_uds`](../../components/e32p_uds) and its ISO-TP glue.

## What the ECU offers

On `0x7E1` (requests) / `0x7E9` (answers), ECU address `0x11`, 11-bit IDs:

| Service | What it does here |
|---|---|
| `10` sessions | default, programming, extended; falls back to default after 5 s of silence |
| `22` read | `F190` VIN, `F18C` serial number, `F195` the running image's version, `0100` a setting |
| `2E` write | `0100` — extended session, unlocked; kept in NVS across resets |
| `27` security | key = HMAC-SHA256(secret, seed), first 4 bytes; 3 wrong keys lock it out for 10 s, and the lockout survives a reset (see "Seed and lockout") |
| `19` / `14` | read and clear trouble codes |
| `31` routines | `0201` stores P0217 so there is a code to read; `FF00` erase; `FF01` check |
| `34` `36` `37` | download an image, 1024 bytes per block |
| `11` | reset |
| `3E` | tester present |

## Build and flash

The default is **500 kbit**, what vehicles and PC tools expect:

```powershell
idf.py -C examples/33_uds_server -p COM5 build flash monitor
```

**On the HIL bench, which runs at 250 kbit**, add the bench overlay (its own build
folder and config file, so the two never mix):

```powershell
idf.py -C examples/33_uds_server -B examples/33_uds_server/build_bench `
  -D SDKCONFIG=sdkconfig.bench `
  -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.bench250k" -p COM5 flash
```

At boot it prints one line, e.g.
`@EX33 READY slot=ota_0 ver=33-0.1.0 sink=ram provisioned=no rate=250000`.

If the bus goes bad, the ECU logs its CAN error counters and recovers from bus-off by
itself.

## Use it from the PC

Needs `python-can`, `can-isotp` and `udsoncan`, and a CANable at the same bitrate.
Close Cangaroo first — the CANable's port can only be open in one program.

**1. Provision the security secret** (one time per board) over the board's USB serial port:

```powershell
python examples/33_uds_server/tools/uds_client.py provision --port COM5
```

This writes a fresh random secret to the board and keeps a copy in
`tools/.uds_secret`, which is git-ignored. The secret is never printed.

Provisioning works **once**. If the board already holds a secret, it answers
`@PROVISION REFUSED`, writes nothing, and `provision` stops with exit code 1 and leaves
`tools/.uds_secret` as it was. This is on purpose: anyone with a USB cable could
otherwise swap the secret.

**Giving a bench board a new secret is an owner action.** The old secret has to be
removed first: erase the board's `nvs` partition with `esptool erase_region`. This
example uses the repo-root `partitions.csv`, where `nvs` is at 0x9000, 0x6000 bytes. Then
run `provision` again. That erase also removes the stored setting (`0100`) and the
lockout flag. Do it only on a board you own, and keep `tools/.uds_secret` in step with
the board.

**2. Walk the whole flow:**

```powershell
python examples/33_uds_server/tools/uds_client.py flow --canable COM8 --bitrate 250000
```

Each step prints `ok` with its time; the run starts by printing the image and options,
and ends with `all 17 steps passed` (19 with `--reset`), or stops at the first failure with
a non-zero exit code. Add `--bad-crc` to check that a wrong image is caught, and `--reset`
to finish with an ECU reset and a read of the version the ECU now runs (two more steps).
One step asks for the results of the check routine (`31 03 FF01`) before any start; the
ECU must answer `0x24` (request sequence error). That answer is only right once per boot,
so reset the ECU between two runs.

## RAM or a real reflash

Set in `menuconfig` → `33_uds_server` → *Where a download goes*:

- **RAM** (default) keeps the image in memory and compares its CRC-32 with the one the
  tester sends to `FF01`. Safe anywhere. The erase still takes 2 seconds on purpose, so
  the ECU really has to say "please wait" (`0x78`) and the tester really has to wait.
- **OTA** writes the image into the other firmware slot. `FF01` reports whether the image
  was accepted, and `11 01` boots it. If the new firmware never confirms itself, the
  bootloader rolls back to the old one.

## A real reflash at 1 Mbit

Build two images of this example: **A** to run, **B** to download. B is the same
firmware under another version, so you can see which one runs.

```powershell
# image A, 33-0.1.0: flashed over USB
idf.py -C examples/33_uds_server -B examples/33_uds_server/build_ota1m -D SDKCONFIG=sdkconfig.ota1m `
  -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.ota1m" -p COM5 flash
# image B, 33-0.2.0: only built, then sent over CAN
idf.py -C examples/33_uds_server -B examples/33_uds_server/build_ota1m_b -D SDKCONFIG=sdkconfig.ota1m_b `
  -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.ota1m;sdkconfig.defaults.version_b" build
```

Provision the secret (see above), set the CANable to 1 Mbit, then:

```powershell
# a damaged image: must be refused, and A must keep running
python examples/33_uds_server/tools/uds_client.py flow --canable COM8 --bitrate 1000000 --sink ota `
  --image examples/33_uds_server/build_ota1m_b/uds_server.bin --corrupt --reset --expect-version 33-0.1.0
# an image that is not signed with the bench key: must be refused, sent unchanged
python examples/33_uds_server/tools/uds_client.py flow --canable COM8 --bitrate 1000000 --sink ota `
  --image examples/33_uds_server/build_ota1m_b/uds_server-unsigned.bin `
  --expect-refused --reset --expect-version 33-0.1.0
# the good image: must boot from the other slot
python examples/33_uds_server/tools/uds_client.py flow --canable COM8 --bitrate 1000000 --sink ota `
  --image examples/33_uds_server/build_ota1m_b/uds_server.bin --reset --expect-version 33-0.2.0
```

### Signed reflash

The OTA builds (`sdkconfig.defaults.ota1m`) check **who signed** a downloaded image, not
only that it is intact. This is "signed on update": no eFuse is burned and secure boot
stays off. The running image refuses a download that is not signed with the bench key.
Make the key once, in this folder:

```powershell
espsecure.py generate_signing_key --version 1 --scheme ecdsa256 secure_boot_signing_key.pem
```

`--version 1 --scheme ecdsa256` is what an ESP32 build at minimum chip revision 0 allows.
The key is a throwaway test key. `.gitignore` here lists `*.pem`; never commit it. A
product key belongs in a signing service, not in this folder. Both builds (A and B) must
be signed with the same key. The build writes two files: `uds_server.bin` (signed, the one
to send) and `uds_server-unsigned.bin` (what the `--expect-refused` run sends). The setup
is the same as in example 28 and is described in
[`docs/SECURITY_HARDENING.md`](../../docs/SECURITY_HARDENING.md).

### A bench with the HIL companion

The HIL companion board runs its own UDS server on the **same CAN IDs** (`0x7E1` /
`0x7E9`). With both on the bus at the same bitrate, two ECUs would answer one request. So
a 1 Mbit run needs the whole bench at 1 Mbit: the companion on the HIL agent's 1 Mbit
image (`tests/hil/agent/build_s3_1m`), the DUT on image A, and the CANable at 1 Mbit. The
companion's image is the Phase-1 one, not rebuilt for this example.

**Every board on the bus must run at 1 Mbit, or be unplugged from it.** A board left in
its bootloader does not drive its CAN pin and corrupts the traffic — we measured that
(`tests/hil/WIRING.md` §12.5).

The image check catches a damaged image, and the signature check catches an image that
was not signed with the bench key (see "Signed reflash"). Secure boot is not on, so a
person with USB access can still flash anything; see the audit below.

## Security — read this before reusing the example

**This example is for the bench only. It is not a product.**

The secret reaches the board over the **USB serial console**, and that is the only way it
gets there. Provisioning is one-time, so a board that has a secret cannot be re-keyed over
the console, but anyone with a USB cable and physical access to a board that has none can
set it. That is fine on a bench and wrong for a product: turn
`CONFIG_EX33_CONSOLE_PROVISIONING` off, or lock it, in any production build. A product must
not ship with it unlocked. This, and the other points the example raises, are in the
**required security audit** in [`docs/SECURITY_HARDENING.md`](../../docs/SECURITY_HARDENING.md).
That audit has not been done, and its first point must not be skipped.

### Seed and lockout

- **Seed source.** Without Wi-Fi or Bluetooth, the ESP32 random number generator is only
  pseudo-random. `app_main` calls `bootloader_random_enable()` first, which turns the SAR
  ADC noise source back on, so the `27 01` seeds are random. This example uses no ADC, I2S,
  Wi-Fi or Bluetooth, so nothing conflicts. The seed and key stay 4 bytes long.
- **Security flag.** The server remembers in NVS (namespace `ex33`, key `sec_fail`) that
  the last security access ended in a wrong key. After a reset it then answers `27 01` with
  `0x37` (required time delay not expired) for 10 s, so a reset does not clear the lockout.
  A right key clears the flag. A missing flag counts as "no failure". That is right on a
  board that has run this image since its NVS was set up. It is a limit on a board that
  was upgraded in place from an older image: that image stored a secret but never wrote
  the flag, so a wrong key made just before the upgrade is not carried over. An
  unreadable flag counts as a failure.

## Measured on the bench (2026-10-06)

WROOM-32 at 250 kbit, PC tester through a CANable: all 16 steps pass (the flow had 16
steps then); the 2-second erase is answered with `0x78` and udsoncan waits it out; a 4 KB
image arrives intact (CRC-32 match), and a deliberately wrong CRC is reported as a
mismatch. Download speed through the CANable is about 3.6 kB/s.

**2026-10-07, real reflash at 1 Mbit:** the damaged image is refused (`0x72`) and the
old one keeps running; the good one boots from the other slot, confirms itself, and is
still there after another reset. A 290 KB image takes **9.8 s** (29.4 kB/s): the ECU
asks the PC to send the frames of each block back to back. When it asked for a 1 ms
gap between frames, the same download took 77 s.

**2026-11-03, after the security changes:** the same bench setup as 2026-10-06 (WROOM-32,
250 kbit, CANable). `flow --reset` ended with all 19 steps passed, and `flow --bad-crc
--reset` also passed 19 steps, with the ECU reporting a CRC mismatch as it must. A run
without `--reset` has 17 steps (it passed 17 on the same day). The flow includes the step
that asks for the check results before any start (`31 03 FF01`, answered `0x24`).
