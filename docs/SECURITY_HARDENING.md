# Security Hardening — Signed OTA, Secure Boot, Flash Encryption

How to take the platform's OTA path from "works on a trusted network" to
"tamper-proof in the field," and what each step costs. Read this before shipping
a device that updates itself.

> ⚠️ **Two of these features burn eFuses and are IRREVERSIBLE.** Secure Boot v2
> and Flash Encryption permanently change the chip; a lost key or a wrong setting
> can brick or lock a device forever. **Never enable them on a bench/dev board you
> care about** — practise on a sacrificial board, and automate them in production.
> The signed-OTA layer below burns nothing and is safe to run anywhere.

> 🔒 **A security audit is required before any production use — and it has not been
> done.** See [Security audit — required before production](#security-audit--required-before-production)
> at the end. It is not optional, and its first point — how the UDS security secret is
> provisioned — must not be skipped.

## What each layer protects against

| Threat | Signed OTA | Secure Boot v2 | Flash Encryption |
|--------|:---------:|:--------------:|:----------------:|
| A network attacker pushes fake firmware via OTA | ✅ rejected | ✅ | — |
| Someone physically reflashes the bootloader/app | — | ✅ refuses to boot | — |
| Someone dumps flash to read your firmware / keys / secrets | — | — | ✅ unreadable |
| Downgrade to an old, vulnerable signed version | anti-rollback | anti-rollback | — |
| eFuses burned (irreversible) | **no** | **yes** | **yes** |

The layers stack: **signed OTA** stops remote spoofing (cheap, reversible);
**Secure Boot v2** adds boot-time verification so physical reflashing can't run
foreign code; **Flash Encryption** keeps the binary and secrets secret at rest.

## Chip-revision note (important)

The app signature scheme depends on the chip:

- **ESP32 built for rev < 3** (the default: minimum revision 0): **ECDSA (v1-style)**
  signing only — `CONFIG_SECURE_SIGNED_APPS_ECDSA_SCHEME`. Secure Boot v2 (RSA) is
  **not** selectable in such a build.
- **ESP32 rev 3 (ECO3), S2, S3, C3, C6, H2**: **RSA-3072 Secure Boot v2**
  (`CONFIG_SECURE_SIGNED_APPS_RSA_SCHEME`) — the modern, recommended scheme. On an
  ESP32 the build must set a minimum chip revision of 3 or higher
  (`CONFIG_ESP32_REV_MIN_3`).

Signed OTA (below) works on all of them.

**This workspace's boards (updated 2026-11-03).** The DUT (COM5) is an
**ESP32-D0WD-V3, rev v3.1** (esptool output, flash logs of 2026-10-07/08), not rev v1.0
as an older note said. So Secure Boot v2 (RSA) would run on it. The bench builds still
use ECDSA signed-on-update because they target minimum revision 0. The product overlay
(see "Product overlay and the owner-run burn procedure") sets minimum revision 3: an
image built with it does not run on a chip older than v3.0. The companion (COM7) is an
ESP32-S3, rev v0.2.

---

## Layer 1 — Signed OTA (no eFuses, safe, reversible) — demonstrated in `28_secure_ota`

Every OTA image must carry a valid signature made with **your private key**, or
`esp_ota_end()` / `esp_https_ota` refuses it. The bootloader is not locked, so a
device can still be recovered over USB — this defends against **remote** attackers,
not physical access. Nothing is burned; you can turn it back off.

**1. Generate a signing key (keep it secret — this is the root of trust):**
```powershell
espsecure.py generate_signing_key --version 1 --scheme ecdsa256 secure_boot_signing_key.pem   # ESP32 rev <3
# espsecure.py generate_signing_key --version 2 --scheme rsa3072 secure_boot_signing_key.pem   # rev 3 / S2 / S3 / C3
```

**2. Enable it (see `examples/28_secure_ota/sdkconfig.defaults`):**
```
CONFIG_SECURE_SIGNED_APPS_NO_SECURE_BOOT=y
CONFIG_SECURE_SIGNED_APPS_ECDSA_SCHEME=y          # or _RSA_SCHEME on rev 3+
CONFIG_SECURE_SIGNED_ON_UPDATE_NO_SECURE_BOOT=y   # verify signature on every OTA
CONFIG_SECURE_BOOT_SIGNING_KEY="secure_boot_signing_key.pem"
```
The build now signs `app.bin` automatically and embeds the public key for
verification.

**3. Result.** Any OTA image without a valid signature is rejected before it can
boot. The `28_secure_ota` demo proves it: it clones its own image with one byte
flipped, and the update is refused (`esp_ota_end` → validation failed) while the
device keeps running its trusted firmware.

**Also in `33_uds_server` (the UDS reflash).** The `sdkconfig.defaults.ota1m` overlay
turns on the same ECDSA signed-on-update, with a git-ignored throwaway bench key
(`examples/33_uds_server/secure_boot_signing_key.pem`). The running image then refuses an
unsigned, wrongly signed or corrupted image sent through the UDS reflash (negative
response `0x72`); a correctly signed one is accepted. Measured on the bench in
slice F (2026-10-21). Nothing is burned. It stops bus attackers, not someone with a USB
cable, and the bench key must never sign a real product.

**Proving authenticity (not just integrity):** flip-a-byte also fails the SHA
checksum, so to prove the *signature* specifically is what's checked, build a
second image, re-sign it with a **different** key (`espsecure.py sign_data
--version 1 --keyfile rogue.pem ...`), serve that via `26_ota_http`, and observe
it rejected while the correctly-signed image is accepted.

---

## Layer 2 — Secure Boot v2 (burns eFuses — production hardware only)

The bootloader verifies the app's signature at **every boot**, and the ROM
verifies the bootloader — so a physically-reflashed or tampered image simply won't
run. Enabling it **burns the public-key digest and control bits into eFuses,
permanently**.

```
CONFIG_SECURE_BOOT=y
CONFIG_SECURE_BOOT_V2_ENABLED=y          # rev 3 / S2 / S3 / C3 (RSA-3072)
CONFIG_SECURE_BOOT_SIGNING_KEY="secure_boot_signing_key.pem"
```

Process (do this deliberately, on real product hardware):
1. Build. The first boot after flashing burns the key digest and enables Secure
   Boot — **from then on the device only boots images signed with that key.**
2. Losing the private key means you can never update that device again. Store it
   in an **HSM** or a locked signing service; never in the repo or a laptop.
3. Test the whole flow on a **sacrificial board** first.

Anti-rollback (`CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK=y` + a secure version) uses
eFuses to stop downgrading to an older signed image with a known vulnerability.

---

## Layer 3 — Flash Encryption (burns eFuses — production hardware only)

Encrypts the firmware (and optionally NVS) in flash with a per-device key held in
eFuses, so a flash dump reveals nothing. Use **Release** mode in production
(Development mode is reversible and for testing only).

```
CONFIG_SECURE_FLASH_ENC_ENABLED=y
CONFIG_SECURE_FLASH_ENCRYPTION_MODE_RELEASE=y    # irreversible; do NOT use on a dev board
```

Combine with Secure Boot for a device that is both **authenticated** (only your
firmware runs) and **confidential** (nobody can read it).

---

## Product overlay and the owner-run burn procedure (example 33)

Added 2026-11-03 (security remediation, slice J). **Nothing here has been flashed to a
board.** The overlay was only *built*, in a scratch folder, with no key and no eFuse
command. The burns below are the owner's to do, on a sacrificial board, and the owner
signs off the result. Until then this document stays "NOT DONE".

**Files.** `examples/33_uds_server/sdkconfig.defaults.product` and
`partitions_product.csv` (platform root, next to `partitions.csv`). Use the overlay
**last** in the defaults list, with its own `SDKCONFIG` file (never the bench one):

```
SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.ota1m;sdkconfig.defaults.product"
```

**What the overlay sets, and what a scratch build resolved it to** (2026-11-03,
ESP-IDF v5.3.1, build exit 0, 0 warnings, no unknown-symbol warning):

| Setting | Symbol | Resolved |
|---------|--------|----------|
| Product marker | `CONFIG_E32P_PRODUCT_BUILD` | `y`, so the hidden `CONFIG_E32P_BENCH_FORBIDDEN` is `y` |
| Minimum chip revision 3.0 | `CONFIG_ESP32_REV_MIN_3` | `y` (`REV_MIN_FULL` 300) |
| Secure Boot v2, RSA scheme | `CONFIG_SECURE_BOOT`, `_V2_ENABLED`, `CONFIG_SECURE_SIGNED_APPS_RSA_SCHEME` | all `y`; ECDSA scheme and the bench "signed apps without Secure Boot" options are off |
| No signing key in the build | `CONFIG_SECURE_BOOT_BUILD_SIGNED_BINARIES` | not set (images are signed in a separate step) |
| Flash Encryption, Release mode | `CONFIG_SECURE_FLASH_ENC_ENABLED`, `CONFIG_SECURE_FLASH_ENCRYPTION_MODE_RELEASE` | `y`; Development mode off |
| NVS encryption | `CONFIG_NVS_ENCRYPTION` | `y` |
| ROM download mode | `CONFIG_SECURE_DISABLE_ROM_DL_MODE` | `y` (permanently disabled); "allow download mode" off |
| JTAG | `CONFIG_SECURE_BOOT_ALLOW_JTAG` | not selected (the option stays hidden); Secure Boot disables JTAG by default |
| Log level | `CONFIG_LOG_DEFAULT_LEVEL_WARN`, `CONFIG_BOOTLOADER_LOG_LEVEL_WARN` | `y` |
| Signed images required on update | `CONFIG_SECURE_SIGNED_ON_UPDATE` | `y` (set by Secure Boot) |
| Plain-HTTP OTA, TLS checks off | `ESP_HTTPS_OTA_ALLOW_HTTP`, `ESP_TLS_INSECURE`, `ESP_TLS_SKIP_SERVER_CERT_VERIFY` | all off |
| OTA sink, rollback | `CONFIG_EX33_SINK_OTA` (overlay); `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` (inherited from the base `sdkconfig.defaults`, not set by the overlay) | `y` |
| Partition table | `CONFIG_PARTITION_TABLE_CUSTOM_FILENAME`, `CONFIG_PARTITION_TABLE_OFFSET` | `../../partitions_product.csv`, `0xf000` |

The generated `sdkconfig.h` of that build shows `CONFIG_E32P_PRODUCT_BUILD 1` and
`CONFIG_E32P_BENCH_FORBIDDEN 1`. The product builds of the HIL agent, example 26,
example 28 and example 29 stop on their own `#error`. A product build of example 33
*without* signed-on-update stops on the `e32p_ota` check ("a product build must verify
image signatures"). Evidence: the scratch build logs of 2026-11-03, which are kept outside
this repository.

**The partition table moves (a measured change).** With Secure Boot v2 and Flash
Encryption the unsigned bootloader is 40,960 bytes at log level WARN (49,152 at INFO).
Signing adds a 4 KB signature block (45,056 bytes), which still fits below `0xf000`. The
default table offset `0x8000` allows 28,672, so the first product build stopped with
"Bootloader binary size ... too large for partition table offset 0x8000". The IDF Secure Boot v2
guide says the same: Secure Boot and Flash Encryption make the bootloader bigger, and the
table offset may have to move. The overlay puts the table at `0xf000`; `nvs`, `otadata`
and `phy_init` move up behind it, and a 4 KB `nvs_keys` partition (flag `encrypted`)
is added. The two OTA slots and `storage` keep their addresses. A product board and a
bench board therefore have different flash maps. Never flash one kind of image onto the
other.

**What the overlay does not do.**

- **UDS secret provisioning stays as on the bench** (baseline item 1; the owner decided
  that the console stays, see the SEC-03 sections of the audit report): one-time, over
  the USB console, refused when a secret is already stored. No new ingest path was
  invented. A product needs one, and a policy for the PC-side copy.
- With `CONFIG_NVS_ENCRYPTION`, `nvs_flash_init()` makes the NVS keys on the first boot
  and keeps them in `nvs_keys` (itself encrypted by Flash Encryption). It was not run on a
  board.
- It sets no anti-rollback and no key revocation. **The ESP32 holds one Secure Boot v2
  key digest and cannot revoke it** (the ESP32 has no revoke option in ESP-IDF v5.3.1).
  If that key leaks or is lost, the board cannot be re-keyed.
- It does not protect against a lab attacker with glitching equipment (SEC-13, tier T4):
  the remediation plan treats it as out of scope for the bench; for a product the owner
  decides (register row SEC-13).

**Owner-run burn procedure (sacrificial board only; read the ESP-IDF guides for Secure
Boot v2 and Flash Encryption first, they are the source of the exact commands).**

1. **Pick a board you can lose.** Not the bench DUT. The burns cannot be undone, and a
   wrong setting can lock the board for good. No bench board is to be used for this.
2. **Make a signing key for the test.** A new RSA-3072 key
   (`espsecure.py generate_signing_key --version 2 --scheme rsa3072 <file>.pem`),
   kept outside the repository. Never use a bench key (`28_secure_ota`, `33_uds_server`)
   and never reuse this key for a real product.
3. **Build** with the three defaults files and a separate `SDKCONFIG`. The build prints
   "App built but not signed" and the `espsecure.py sign_data --version 2` step.
4. **Sign the app and the bootloader** with `espsecure.py sign_data --version 2` and the
   key. For a real product the key lives in a signing service or an HSM, and signing is a
   CI step. It is never done by hand on a laptop.
5. **Flash over USB once, then keep power on.** On a board that was used before,
   **erase the flash first**: the old bench NVS (`0x9000`-`0xEFFF`) may hold a
   provisioned secret, and part of it survives outside any partition of the product map.
   This is an owner step on the sacrificial board, never on the bench DUT. Then flash the
   board. `idf.py flash` does not write the bootloader when Secure Boot is on, so flash it
   yourself: the signed bootloader with the `esptool` command the build prints (offset
   `0x1000`). That command writes `build/bootloader/bootloader.bin`, so sign that file in
   place, or put the path of the signed file into the command. Then flash the partition
   table, `otadata` and the signed app. The first boot then encrypts the flash and
   burns the eFuses: the Secure Boot v2 key digest, the Flash Encryption key and Release
   mode, JTAG off, and (at app start) ROM download mode off. After that **USB flashing
   no longer works**: the only way to change the firmware is an OTA image signed with
   the same key, so a bad image can brick the board.
6. **Check the result by behaviour.** On the ESP32 all the burns happen during the first
   power-on (the bootloader resets once by itself after enabling flash encryption; the
   app then disables ROM download mode on its next start). There is no window for
   `esptool`, and the WARN-level bootloader log shows little of the burns. So check that
   `esptool` can no longer connect in download mode. Then provision the UDS secret, and
   only then check that an unsigned image sent through the UDS reflash is refused with
   `0x72` (the download service needs security access, and with no secret the seed
   request is refused with `0x22`). Optionally, add a WARN log of
   `esp_secure_boot_enabled()` and `esp_flash_encryption_enabled()` to the app for the
   test (this is an option; the platform does not contain that code).
7. Record the outcome here and in the audit report. Sign-off stays with the owner.

---

## Key management (the part that actually matters)

- The **private signing key is your root of trust.** Anyone who has it can push
  firmware to every device. Keep it in an HSM or a CI signing step, not on a dev
  machine, and **never commit it** (`28_secure_ota/secure_boot_signing_key.pem`
  here is a throwaway TEST key — regenerate a real one for anything shipped, and
  add `*.pem` to `.gitignore`).
- Sign release images in CI/a signing service, not by hand.
- Have a key-rotation and revocation plan before the first device ships.

## Recommended order for a real product

1. **Signed OTA** first (this platform, `28_secure_ota`) — immediate remote
   protection, no risk.
2. On production hardware, add **Secure Boot v2**, tested on a sacrificial board.
3. Add **Flash Encryption (Release)** if the firmware or its data is sensitive.
4. Turn on **anti-rollback** once you have a versioning discipline.

The `e32p_ota` module works unchanged through all of this — signature
verification is handled by ESP-IDF underneath `esp_ota_end` when signing is
enabled; the module just surfaces the pass/fail.

---

## Security audit — required before production

**Status: NOT DONE.** Recorded 2026-10-06. Nothing built on this platform may ship, and
no deliverable may be called production-ready, until an audit has covered every point
below and someone has signed it off. Add points as features land; do not remove one
without recording why.

### 1. UDS security secret, provisioned over the USB serial console — MUST NOT BE SKIPPED

The UDS server (`e32p_uds`, example 33) unlocks security access with a key derived
from a per-device secret held in NVS. That secret is written over the **USB serial
console** by `uds_client.py provision`, not over CAN — decided 2026-10-06 (diagnostics
ticket, Phase 3). The trust boundary is therefore **physical access to the USB port**.
That is the normal model for a bench or a factory line, and it is wrong for a product
unless the following are settled:

- **The provisioning command must not exist in a production build**, or must be locked
  (one-time, or authenticated). Otherwise anyone with a USB cable can replace the secret
  and unlock every diagnostic service, including the reflash.
- **The secret must never be printed, logged or echoed** — on the console, in a crash
  dump, or in a test log.
- **NVS is plaintext** unless Flash Encryption *and* NVS encryption are enabled. Without
  them, anyone who can read the flash can read the secret.
- **The PC-side copy** (a git-ignored file on the provisioning PC) needs a storage,
  access and rotation policy. A file on a laptop is not key management.
- **One secret per device, or one per fleet?** A shared secret means one extracted
  device unlocks all of them.

**Disposition, 2026-11-03 (owner sign-off still open).**
- The command is now one-time in every build: it is refused when the board already holds a
  secret (slice E, SEC-03). `CONFIG_EX33_CONSOLE_PROVISIONING` exists and the overlay
  leaves it on. Turning it off leaves no way to put a secret on the board, so a product
  needs its own ingest path (the owner decision is recorded in the SEC-03 sections of
  the audit report). The overlay adds none.
- The secret is never printed, logged or echoed (checked: no echo, core dump off, GDB stub
  off).
- NVS is plaintext until Flash Encryption and NVS encryption are on. The product overlay
  turns both on; the burn is the owner's (SEC-04, see the register below).
- PC copy: one plaintext file per PC (`examples/33_uds_server/tools/.uds_secret`, git-ignored).
  Provisioning a second board overwrites the first board's copy. There is no rotation and
  no revocation. **Owner:** write the policy; keep one copy per board, for example keyed by
  the F18C serial number.
- Per device or per fleet: per device by construction (each `provision` draws 32 fresh
  bytes from `secrets.token_bytes`). The policy is not written down yet. **Owner.**

### 2. UDS seed / key strength

Seed and key are 4 bytes; the example derives the key as HMAC-SHA256(secret, seed)
truncated to 4 bytes; three wrong keys lock access out for 10 seconds, and switching
sessions does not reset that. Check: the seed comes from a real entropy source
(`esp_random()` is only truly random with the RF on or the bootloader's entropy
enabled); seeds are never reused; and 4 bytes are enough for the threat model.

**Disposition, 2026-11-03.**
- Entropy: example 33 turns on the bootloader's random source first thing in `app_main`
  (SEC-11, fixed for example 33). Bench check: 240 seeds over 60 resets, no repeats and no
  all-zero seed. That check cannot prove entropy and cannot tell the new build from the
  old one, so the verdict rests on the code and the IDF rule. The HIL agent's seed
  generator still has no entropy call; its secret is a public test value.
- Lockout: a failed key is saved, and the server waits after a reset when the last attempt
  failed or the flag is unknown (SEC-12, fixed; checked on the bench).
- **Open, owner:** the seed and key are still 4 bytes. Seed requests are not counted, so
  someone who has recorded N genuine seed and key pairs can keep asking for seeds until
  one of the recorded seeds comes back and then replay its key. With 4-byte seeds that
  takes about 2^32 / N seed requests. At about 250 requests a second that is roughly 200
  days for N = 1 and about 2 days for N = 100. The lockout does not slow this down.
  Accepted by the owner for the bench. For a product: decide the key length and whether to
  count seed requests.

### 3. UDS reflash accepts any image the sink accepts

With the key, a tester can flash any image that passes the sink's checks. Unless images
are **signed** (signed OTA, then Secure Boot v2 — Layers 1 and 2 above), security access
alone is all that stands between the bus and arbitrary code.

Measured 2026-10-07 (TEST_PLAN A-12): the image check refuses a corrupted image (one
flipped byte: checksum failure, `0x72`). That is integrity, not authenticity — a
well-formed image from anyone passes.

**Disposition, 2026-11-03.** Example 33's `ota1m` builds now check a signature (bench
key, ECDSA, no eFuses) and refuse unsigned, wrongly signed and corrupted images with
`0x72` (slice F, SEC-02 fixed). That stops the bus attacker. A product uses Secure Boot
v2 from the overlay; the burn is the owner's (SEC-04, SEC-09).

### 4. Test-only material must never reach a product build

- The HIL agent's fixed UDS test secret, `UDS_TEST_SECRET` in
  `tests/hil/agent/main/hil_diag.c` — in source on purpose, written to NVS by the
  companion at boot (diagnostics ticket, Phase 3, decision 2).
- `CONFIG_ESP_HTTPS_OTA_ALLOW_HTTP=y` in the HIL agent (plain-HTTP OTA for the bench).
- The throwaway signing keys in `examples/28_secure_ota` and `examples/33_uds_server`.

**Disposition, 2026-11-03.** Gated by the product marker `CONFIG_E32P_PRODUCT_BUILD`
(and by Secure Boot or Flash Encryption, which count as a product). With it set, a build
of the HIL agent, example 26, example 28 or example 29 stops with `#error`. `e32p_ota`
stops with `#error` if plain-HTTP OTA or a disabled TLS check is on, or if
`CONFIG_SECURE_SIGNED_ON_UPDATE` is off. It also stops if the product marker is set and
`CONFIG_SECURE_BOOT` is off, because the bench "signed apps, no Secure Boot" option also
sets `CONFIG_SECURE_SIGNED_ON_UPDATE` and trusts the bench ECDSA keys. The product
overlay turns off `CONFIG_SECURE_BOOT_BUILD_SIGNED_BINARIES`, so no bench key signs a product image.

**Remaining bench material, with a never-reuse rule.** These are not gated by a build
check, so the rule is a written one:
- `HIL_TEST_SECRET` in `tests/hil/host/test_uds_canable.py` (PC-side test code). It is the
  same value as `UDS_TEST_SECRET` in the HIL agent firmware. It is public. It must never
  become a real device secret.
- `examples/33_uds_server/tools/.uds_secret`: the PC copy of the bench board's secret.
  Never provision another board from it, and never use it for a product.
- The bench signing keys of examples 28 and 33 (`secure_boot_signing_key.pem`, git-ignored).
  Never sign a product image with them, and never burn a key digest made from them.
- The bench Wi-Fi network name and password (SEC-08). The owner accepted the risk for the
  bench (see the register). A product or any other network must use new credentials.
- `e32p_ble` (used by example 27 and example 29) pairs with "Just Works" and allows
  bonding (`components/e32p_ble/src/e32p_ble.c`: no input/output, `sm_mitm` 0, bonding on).
  It has no guard of its own, and example 27 has no product guard. A product must not
  ship `e32p_ble` as it is. It needs authenticated pairing (passkey or out-of-band data,
  with MITM protection on), or a product `#error`. Pairing alone is not enough: the
  characteristics must also require an encrypted, authenticated link (NimBLE
  `BLE_GATT_CHR_F_READ_AUTHEN` and `BLE_GATT_CHR_F_WRITE_AUTHEN`, or the `_ENC` flags).
  Today `e32p_ble` sets only READ, WRITE and NOTIFY (`chr_flags()` in `e32p_ble.c`), so a
  peer can read and write them without pairing at all. The `e32p_ble` API cannot express
  these flags yet: `props` is a `uint8_t` with three bits defined, and the NimBLE flags
  are 16 bits wide. Owner action (see the BLE pairing row in the register).

**Never reuse:** nothing in this list may be used on a device that is not a bench board, or
copied into a product's provisioning, build or signing step. If one ever is, treat that
device as compromised.

### 5. What is reachable without security

Review, service by service and session by session, what a tester can do while locked —
DID writes (`0x2E`), routines (`0x31`), clearing trouble codes (`0x14`, which for
emissions-relevant codes is regulated) and the OBD-II services, which by design need no
security at all.

**Disposition, 2026-11-03.** Reviewed in the audit (section 9): the checks run in the
ISO 14229-1 order, `0x2E` needs the extended session and security, routines and download
need session and security. Left as they are: `0x11` ECUReset and `0x14` ClearDTC work
without security, and OBD-II needs none by design. Residual, a product decision (audit
report section 9). See the register.

### 6. Anything on the CAN bus can talk to the diagnostic servers

The servers answer whoever sends the right frames. Whether a gateway should filter
diagnostic traffic depends on the product's architecture; decide it explicitly.

**Disposition, 2026-11-03.** Confirmed in the audit (section 9): classic CAN has no sender
authentication. Architectural, a product decision, not a code defect. Still open for the
product.

### 7. OTA by URL (`e32p_ota_update_from_url`) — added 2026-11-03

Check these before any product pulls firmware over the network:

- **Plain http is refused at run time.** Unless `CONFIG_ESP_HTTPS_OTA_ALLOW_HTTP=y`, a URL
  that does not start with `https://` is refused before anything is sent. An https to
  http redirect is also refused (no image bytes are written), but only after the plain
  request has been sent (see the next bullet). The option is for the bench; a product
  build stops with `#error` if it is on.
- **Redirect policy.** A redirect to a connection without TLS is refused, but only after
  the plain request has been sent. So never put credentials or tokens in the URL: it is
  logged, and a redirect could send them in the clear.
- **Certificate dates** are checked only with `CONFIG_MBEDTLS_HAVE_TIME_DATE=y` and a clock
  that is set. That option is off in every configuration in this workspace. A product must
  turn it on and set the clock, or accept that an expired certificate passes.
- **Bundle scope.** The default bundle trusts every public root CA. It proves the host
  named in the URL, not who chose the URL. A product should build a bundle with its own CA
  only (`CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_NONE` plus a custom bundle).
- **The CA check proves the server, not the image.** Sign the images. A product build
  therefore stops with `#error` unless `CONFIG_SECURE_SIGNED_ON_UPDATE` is on, and also
  unless `CONFIG_SECURE_BOOT` is on when the product marker is set.
- **Network proof is not done.** The https checks (self-signed refused, wrong name refused,
  redirect refused) have not been run against a network yet. They need the bench Wi-Fi
  (`HIL_WIFI_SSID` and `HIL_WIFI_PASS` in the environment). Status: pending.

---

## Owner-action register (2026-11-03)

Things only the owner can do or decide. **Every row is NOT DONE.** Nothing here is a
sign-off, and nothing built on this platform may be called production-ready until the
whole audit above is signed off.

| ID | Owner action | Notes | Status |
|----|--------------|-------|--------|
| SEC-08 | Owner decision 2026-11-03: the bench Wi-Fi is a sandbox-only network; the PSK is not rotated (risk accepted for the bench). Any product or other network must use new credentials. | The bench password was printed once into a Claude session transcript on 2026-11-03 (audit report, "SEC-08 — slices H and I"). No project file holds it. Also decide whether to clean that transcript. | NOT DONE |
| SEC-04 | Burn Secure Boot v2, Flash Encryption (Release) and NVS encryption on a sacrificial board with the product overlay; then on product hardware. | Procedure above. Irreversible. Not the bench boards. | NOT DONE |
| SEC-09 | ROM download mode off and JTAG off, as part of the same burn. | In the overlay. After it, USB flashing no longer works. | NOT DONE |
| SEC-13 | Record that lab-grade fault injection (tier T4) is out of scope, or choose chips and countermeasures from the Espressif advisories. | The remediation plan treats T4 as out of scope; the owner records that. The ESP32 has one Secure Boot digest and no revoke. | NOT DONE |
| SEC-05 | Product BLE provisioning: protocomm security 2 with a per-device proof of possession (PoP). | Example 29's own README lists the gap. | NOT DONE |
| BLE pairing (SEC-05) | A product must not ship `e32p_ble` as it is: add authenticated pairing (passkey or out-of-band data, MITM protection on) and make the characteristics require an encrypted, authenticated link (`_AUTHEN` or `_ENC` flags), or add a product `#error`. | `e32p_ble` pairs with Just Works and bonding, sets only READ, WRITE and NOTIFY on its characteristics (a peer needs no pairing), and its API cannot express the flags yet. It has no guard of its own, and example 27 has no product guard. See the never-reuse list in item 4. | NOT DONE |
| Baseline item 1 | Product ingest path for the UDS secret; PC-side copy policy; one-secret-per-device policy. | See item 1 above. The console path is one-time but is a bench path. | NOT DONE |
| SEC-03-E1 | Decide how to handle a lost `@PROVISIONED` reply. | If the board stores the secret but the PC never saves it, a retry is REFUSED (the board holds a secret the PC does not have). Recovery is an owner NVS erase. Deferred. A fix later: write a pending file first, then rename it. | NOT DONE |
| SEC-11 | Accept or change the 4-byte seed and key; decide on counting seed requests. | Bench numbers (slice E): 240 seeds over 60 resets, 0 repeats (audit report, "Re-test — slice E (2026-11-03)", "SEC-11 — slice E"). Replay note: with N recorded seed and key pairs a replay needs about 2^32 / N seed requests. | NOT DONE |
| SEC-12-E1 / UDS-E03 | For a product: write `sec_fail = 0` at provisioning, and treat "flag missing while a secret is present" as failed. | Today a missing flag on a board upgraded in place counts as "no failure". The gain for an attacker is at most one boot without the delay (about 3 guesses at 2^-32 each). The auditor recommends the product pattern. | NOT DONE |
| Anti-rollback | Decide on anti-rollback. | `CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK` is not set, so a downgrade to an older signed image stays possible. | NOT DONE |
| Item 5 (reachable without security) | Decide whether `0x11` ECUReset and `0x14` ClearDTC may stay open without security in a product. | Residual, a product decision (audit report section 9). OBD-II needs no security by design. | NOT DONE |
| Signing key | Keep the product signing key in a signing service or an HSM. | The ESP32 holds one Secure Boot v2 digest and cannot revoke it. A leaked or lost key cannot be replaced on that board. | NOT DONE |
| UDS deviation (i) | Keep or change: a functional request, while another request is pending, gets `0x21` instead of silence. | `components/e32p_uds/DESIGN.md`, row (i). Not checked against the 2013 text. | NOT DONE |
| UDS-E02 | Decide: routine `31 03 0201` gets `0x12` or `0x24`? | Owner decision beside deviation (i). | NOT DONE |
| UDS-E04 | Decide the order of the request checks. | Needs the 2013 text of ISO 14229-1. Since 2026-11-16 the text library holds ISO 14229-1:2013 (second edition); the check against it has not been run yet. | NOT DONE |
| Bench material | Accept the never-reuse rule in item 4. | HIL test secret, `.uds_secret`, bench signing keys, bench Wi-Fi. | NOT DONE |
| SEC-07 network proof | Provide the bench Wi-Fi in the environment, or run the https checks yourself. | Done 2026-11-03: the owner supplied the bench Wi-Fi and the https checks ran on the bench (audit report, "Re-test — NET Wi-Fi step (2026-11-03)"). Full HIL suite 79 passed, 2 skipped on 2026-11-16. | DONE (bench) |

---

## Owner decisions, 2026-11-16

These record what the owner decided. They are not a sign-off. The audit above stays
**NOT DONE**.

### No eFuse burns until a product ships (SEC-04, SEC-09)

No board in this workspace gets Secure Boot, Flash Encryption, NVS encryption or the
download-mode and JTAG lock. The product overlay of example 33 stays build-only. When a
real product ships, the burns are done then, on that product's hardware, with the
"Owner-run burn procedure" above, starting on a sacrificial board. Until then SEC-04 and
SEC-09 stay open in the register.

### BLE provisioning and pairing: chosen design, not built (SEC-05)

The work on this platform covered UDS. BLE is out of that scope, so this design is written
down and nothing is built yet. A product that uses `e32p_ble` or BLE Wi-Fi provisioning
must do all of this first:

1. **Wi-Fi provisioning (example 29):** protocomm security 2 (SRP6a) with a proof of
   possession (PoP) that is unique to each device, for example printed on its label.
2. **Pairing (`e32p_ble`):** passkey pairing with MITM protection on, instead of "Just
   Works". A board with a display (the bench has a TM1637) can show the passkey.
3. **Characteristics:** each one needs an encrypted, authenticated link (NimBLE
   `BLE_GATT_CHR_F_READ_AUTHEN` / `_WRITE_AUTHEN`, or the `_ENC` flags), so a peer cannot
   read or write without pairing.
4. **API change:** `props` in the `e32p_ble` API is a `uint8_t` with three bits. It has to
   carry the 16-bit NimBLE flags, so examples 27 and 29 change with it.

Until this is built, the never-reuse rule in item 4 applies: do not ship `e32p_ble` as it is.

### UDS secret: chosen direction is certificate-based authentication (baseline item 1)

The bench keeps the one-time USB console provisioning. The owner accepts it for the bench
only. For a product, the chosen direction is to replace the shared secret behind `0x27`
with certificate-based authentication, service `0x29` Authentication from
ISO 14229-1:2020. The device then holds only a public key and its own certificate, so a
device taken apart gives nothing that unlocks another device. The change is being
planned. It is not built.

### Left for product time

These do not change the bench. They are done when a product ships:

- **The eFuse burns** above, including the anti-rollback version field if anti-rollback is on.
- **The product signing key** lives in a signing service or an HSM, and signing runs in CI.
  It is never kept or used on a laptop.
- **A per-device secret, or the device certificates for `0x29`**, are made by a factory
  station, never by hand.
- **The TLS trust store** is cut down to the product's own CA, and the clock is set so
  certificate dates are checked (item 7).
- **Gateway filtering** of diagnostic traffic (item 6) is decided for the vehicle.
