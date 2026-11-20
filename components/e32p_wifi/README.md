# e32p_wifi — Station + SoftAP facade

Thin facade over `esp_wifi`. Bring up a Wi-Fi Station (connect to an AP) or a
SoftAP with a few calls — netif, the default event loop, NVS, and connection
state are handled internally.

## Enable

```
CONFIG_E32P_WIFI_ENABLE=y
```
Then `REQUIRES e32p_wifi`.

## API

| Function | Purpose |
|---|---|
| `e32p_wifi_init` / `_deinit` | Bring up / tear down the stack (idempotent init). |
| `e32p_wifi_sta_connect` | Connect to an AP, blocking until IP or timeout; auto-reconnects. |
| `e32p_wifi_sta_disconnect` | Disconnect and stop reconnecting. |
| `e32p_wifi_is_connected` / `_get_ip` / `_get_rssi` | Connection state, IP, signal. |
| `e32p_wifi_ap_start` / `_ap_stop` | Run a SoftAP. |

## Station example

```c
ESP_ERROR_CHECK(e32p_wifi_init());
if (e32p_wifi_sta_connect("MyNetwork", "secret", 15000) == ESP_OK) {
    char ip[16]; e32p_wifi_get_ip(ip, sizeof(ip));
    ESP_LOGI(TAG, "online at %s", ip);
}
```

## SoftAP example

```c
ESP_ERROR_CHECK(e32p_wifi_ap_start("e32p-demo", "12345678", 1, 4));
```

## Notes / gotchas

| Item | Detail |
|---|---|
| Dependencies | `e32p_wifi_init` calls `e32p_nvs_init` + `e32p_event_init` for you. |
| Blocking connect | `sta_connect` returns `ESP_OK`, `ESP_ERR_TIMEOUT`, or `ESP_FAIL` (wrong password / no AP after 5 retries). |
| Secrets | Never hardcode credentials — pass them in from NVS or Kconfig. The example uses menuconfig. |
| ADC2 | While Wi-Fi runs, ADC2 reads fail (`e32p_adc` maps that to `ESP_ERR_E32P_RESOURCE_CONFLICT`). Use ADC1. |
| Password rules | SoftAP passwords must be 8–63 chars (or open if empty). |
