# e32p_ota — over-the-air firmware update

A facade over ESP-IDF's `esp_ota_ops` + `esp_https_ota`: find out what's running,
stream a new image into the spare slot (from a URL or any source), boot it, and
confirm or roll back.

## What it does

- **Query (offline):** running slot label, next update slot, running app
  version/SHA/build date, and the running image size.
- **Rollback:** confirm a fresh image (`mark_valid`), check if rollback is
  possible, or revert to the previous app (`rollback_and_reboot`).
- **Streaming update:** `begin` → `write` (repeat) → `end` — feed image bytes
  from anywhere (an HTTP body, an MQTT payload, another partition). Starting a new
  update cancels a staged image that has not booted yet: `begin` points boot back
  at the running slot before it erases, and erases nothing if that fails. The URL
  pull does the same before it sends any request. With
  `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`, the cancel also confirms the running
  image, so it stays valid and does not become pending-verify.
- **URL pull:** `update_from_url()` downloads a `.bin` via HTTP(S) straight into
  the next slot. Like `begin`, it first cancels a staged image that has not booted yet.

## Design

The query + rollback layer is a thin, **offline** wrapper — unit-tested on-device
(labels, version, image size, arg validation). The image *download* needs a
network, so it lives in the `25_ota` demo, which does a **self-contained self-OTA**
(clones the running image into the other slot and boots it) to prove the whole
`begin/write/end/set-boot/rollback` path with no server. The public header hides
the esp_ota API so the module cherry-picks cleanly.

> Needs an **OTA-capable partition table** (`otadata` + `ota_0`/`ota_1`). The
> platform's default `partitions.csv` has one. A single-app (`factory`) table has
> no OTA slots — `next_label()` returns NULL and `begin()` returns `ESP_ERR_NOT_FOUND`.

## API

```c
/* queries (offline) */
const char *e32p_ota_running_label(void);
const char *e32p_ota_next_label(void);
esp_err_t   e32p_ota_get_app_info(e32p_ota_app_info_t *out);
esp_err_t   e32p_ota_running_image_size(size_t *out_bytes);
bool        e32p_ota_is_pending_verify(void);
bool        e32p_ota_rollback_possible(void);

/* rollback */
esp_err_t   e32p_ota_mark_valid(void);
esp_err_t   e32p_ota_rollback_and_reboot(void);   /* no return on success */

/* streaming update */
esp_err_t   e32p_ota_begin(e32p_ota_handle_t *out);
esp_err_t   e32p_ota_write(e32p_ota_handle_t h, const void *data, size_t len);
esp_err_t   e32p_ota_end(e32p_ota_handle_t h);     /* validate + set boot */
esp_err_t   e32p_ota_abort(e32p_ota_handle_t h);

/* HTTP(S) pull */
esp_err_t   e32p_ota_update_from_url(const char *url, uint32_t timeout_ms);
```

## Usage

Confirm a freshly-booted update, then poll a server for the next one:

```c
if (e32p_ota_is_pending_verify()) {
    /* the app runs — mark it good so it isn't rolled back on the next reset */
    e32p_ota_mark_valid();
}

if (e32p_ota_update_from_url("http://192.168.0.41:8000/app.bin", 0) == ESP_OK) {
    esp_restart();   /* reboot into the new image */
}
```

## Trying the URL path (local server)

Build any example, then serve its `.bin` from your PC and point the device at it:

```powershell
# in the build directory of the app you want to push:
python -m http.server 8000
# then call, from firmware:
#   e32p_ota_update_from_url("http://<your-pc-ip>:8000/<app>.bin", 0);
```

An `https://` URL is checked against the ESP-IDF CA bundle (needs
`CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=y`, the ESP-IDF default). For a plain `http://` URL
you must enable **`CONFIG_ESP_HTTPS_OTA_ALLOW_HTTP=y`** (menuconfig: *Component config →
ESP HTTPS OTA → Allow HTTP for OTA*). Without it, `update_from_url()` returns
`ESP_ERR_INVALID_ARG` for any URL that does not start with `https://`, before it
touches the network. That's fine for a trusted LAN test but insecure, and a product
build refuses it (see below). An https pull needs stack in the calling task: see
[Caller stack](#caller-stack).

### Caller stack

`update_from_url()` runs the whole pull, TLS handshake included, in the calling task.
It starts no task of its own and does not check the stack. Give the calling task
**8 KB of stack (at least 6 KB)**. Bench figures (ESP32, HIL agent main task of 8192 B):
`uxTaskGetStackHighWaterMark()`, read after the call, returns the minimum free stack,
so used = 8192 - free. A TLS handshake with the image then rejected left 4436 B free
(3756 B used). Two full https downloads staged into the spare slot left 4292 B and
4180 B free (3900 B and 4012 B used), so a full download uses about 4.0 KB. 6 KB
(6144 B) leaves about 2.1 KB over the 4012 B peak; another server certificate chain or
other mbedTLS settings can need more. At DEBUG log level the call logs the
caller's free stack when it starts.

## Rollback safety

Enable `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y` and a freshly-flashed image boots
in **pending-verify**: if it doesn't call `e32p_ota_mark_valid()` before the next
reset, the bootloader automatically reverts to the previous working app. That's
the safety net for a bad update — the `25_ota` demo shows it.

## ⚠️ Security & production hardening

OTA replaces the code your device runs, so a compromised update path is a full
device compromise. Read this before shipping.

**What `update_from_url()` does now:** an `https://` server is verified against the
ESP-IDF CA bundle. If the certificate does not chain to a bundled root CA, the call
fails and no image bytes are written. The bundle must be on (`CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=y`);
without it this module does not compile. Unless `CONFIG_ESP_HTTPS_OTA_ALLOW_HTTP=y`,
plain `http://` is refused at run time (`ESP_ERR_INVALID_ARG`), and so is a redirect
that opens a connection without TLS (`ESP_ERR_INVALID_RESPONSE`; no image bytes are written to
flash). Do not put credentials or tokens in the URL: it is logged, and a redirect to
`http://` is refused only after the plain request has been sent.

**What this does not cover:**

- The full CA bundle trusts every public root CA. It proves the host named in the URL,
  not who chose the URL and not who made the image. A product can build a bundle with
  its own CA only (`CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_NONE` plus
  `CONFIG_MBEDTLS_CUSTOM_CERTIFICATE_BUNDLE`).
- Certificate dates are checked only with `CONFIG_MBEDTLS_HAVE_TIME_DATE=y` and a clock
  that is set. That option is off in every configuration in this workspace.

**What's unsafe (and must NOT reach production):**

- **Plain HTTP** (`http://` + `CONFIG_ESP_HTTPS_OTA_ALLOW_HTTP=y`). Refused at run
  time unless that option is on. No encryption,
  no server authentication — anyone on the network path (a man-in-the-middle) can
  serve a malicious binary and the device will flash and boot it. This flag exists
  **only for local testing** (e.g. the `26_ota_http` demo pulling from
  `python -m http.server`). It is off by default for exactly this reason. A build
  with `CONFIG_E32P_BENCH_FORBIDDEN` set (the product marker `CONFIG_E32P_PRODUCT_BUILD`,
  Secure Boot or Flash Encryption) stops with a compile error if
  `CONFIG_ESP_HTTPS_OTA_ALLOW_HTTP`, `CONFIG_ESP_TLS_INSECURE` or
  `CONFIG_ESP_TLS_SKIP_SERVER_CERT_VERIFY` is on.
- **No image signing.** Without Secure Boot, nothing at boot verifies the image
  came from you — a tampered binary in the OTA slot runs. A build with
  `CONFIG_E32P_BENCH_FORBIDDEN` set also stops with a compile error unless
  `CONFIG_SECURE_SIGNED_ON_UPDATE` is on (Secure Boot, or signed-on-update without
  Secure Boot), so a product cannot ship this module with signature checks off. A build
  with the product marker `CONFIG_E32P_PRODUCT_BUILD` also stops unless Secure Boot
  (`CONFIG_SECURE_BOOT`) is on: the bench "signed apps, no Secure Boot" option trusts
  throwaway bench ECDSA keys, and on the ESP32 Secure Boot v2 uses RSA, so those keys
  cannot sign a product image.

**How to do it properly:**

| Layer | Testing | Production |
|-------|---------|-----------|
| Transport | `http://`, `ALLOW_HTTP=y` | `https://`, `ALLOW_HTTP` **off** (build guard; run-time refusal of http) |
| Server auth | none | done by the module: the server cert is verified against the ESP-IDF CA bundle |
| Image authenticity | none | **Secure Boot v2** — the bootloader refuses an unsigned/tampered image |
| Confidentiality at rest | none | **Flash Encryption** if the binary itself is sensitive |

The CA bundle shows who the server is. It does not show that the image is genuine,
so for production also keep `ALLOW_HTTP` off and sign the images (signed OTA or
Secure Boot).

**Signed OTA** (image signature verification) is the next hardening step and works
on any target without burning eFuses: the `28_secure_ota` example enables it and
demonstrates a tampered image being rejected. The full production process —
signed OTA, Secure Boot v2, and Flash Encryption, with the eFuse warnings — is in
[`docs/SECURITY_HARDENING.md`](../../docs/SECURITY_HARDENING.md).

## Cherry-pick

```
CONFIG_E32P_OTA_ENABLE=y
```

Requires `e32p_core`; privately requires `app_update`, `esp_https_ota`,
`esp_http_client`, `esp_partition`, `esp_app_format`, `mbedtls` (CA bundle; keep
`CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=y`). Use an OTA partition table.
