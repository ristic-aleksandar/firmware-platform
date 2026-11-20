# e32p_nvs — NVS key-value store facade

Wrapper over the ESP-IDF NVS flash store. Two ways to use it:

- **Namespace handle** — open a namespace once, do many reads/writes, commit
  explicitly, close. Efficient (one open, batched writes) and the right choice
  for a subsystem that owns its settings.
- **One-shot helpers** — `read_i32` / `write_i32` / `read_str` / `write_str` for
  the occasional single value (they open, do the op, commit, and close for you).

## Enable

```
CONFIG_E32P_NVS_ENABLE=y
```
Then `REQUIRES e32p_nvs`. Call `e32p_nvs_init()` once at startup.

## Handle usage (preferred for grouped access)

```c
ESP_ERROR_CHECK(e32p_nvs_init());

e32p_nvs_handle_t h;
ESP_ERROR_CHECK(e32p_nvs_open("wifi", E32P_NVS_READWRITE, &h));
ESP_ERROR_CHECK(e32p_nvs_set_str(h, "ssid", "my-net"));
ESP_ERROR_CHECK(e32p_nvs_set_i32(h, "channel", 6));
ESP_ERROR_CHECK(e32p_nvs_commit(h));      /* one flush for both writes */
ESP_ERROR_CHECK(e32p_nvs_close(h));
```

## One-shot usage

```c
int32_t boots = 0;
esp_err_t err = e32p_nvs_read_i32("storage", "boot_count", &boots);
if (e32p_nvs_is_not_found(err)) boots = 0;         /* first boot */
ESP_ERROR_CHECK(e32p_nvs_write_i32("storage", "boot_count", boots + 1));
```

## Notes / gotchas

| Item | Detail |
|---|---|
| Init | Auto-erases + retries once on `NO_FREE_PAGES` / `NEW_VERSION_FOUND`. Idempotent. |
| Commit | Writes are staged until `e32p_nvs_commit` (handle) — the one-shot writers commit for you. |
| Missing key | `get_*` / `read_*` report "not found" — test with `e32p_nvs_is_not_found(err)` (no `nvs.h` needed). |
| Namespace / key | Max 15 chars each. |
| Strings | `get_str` with `out=NULL` returns the required length in `inout_len` — size, then read. |
| Not initialized | Handle ops before `e32p_nvs_init` return `ESP_ERR_E32P_NOT_INIT`. |

The partition used is the default `nvs` partition from the platform
`partitions.csv` (24 KB).
