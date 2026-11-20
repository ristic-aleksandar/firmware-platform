# 12_wifi_sta

Connects to a Wi-Fi network and prints the IP address + signal strength. Proves
`e32p_wifi` in Station mode.

## Set your credentials (once)

Credentials are **not** in source. Set them via menuconfig:

```powershell
idf.py -C C:\Users\aleks\Work\esp32_platform\examples\12_wifi_sta menuconfig
# -> "Example Wi-Fi Configuration" -> set WiFi SSID and WiFi Password
```

(Or create a local, un-committed `sdkconfig` with `CONFIG_E32P_EXAMPLE_WIFI_SSID`
/ `CONFIG_E32P_EXAMPLE_WIFI_PASSWORD`.)

## Build & flash

```powershell
. C:\Espressif\Initialize-Idf.ps1 -IdfId esp-idf-ab7213b7273352b64422b1f400ff27a0
idf.py -C C:\Users\aleks\Work\esp32_platform\examples\12_wifi_sta set-target esp32
idf.py -C C:\Users\aleks\Work\esp32_platform\examples\12_wifi_sta -p COM5 build flash monitor
```

Expected:

```
I (…) wifi_sta: connecting to "MyNetwork"...
I (…) e32p_wifi: wifi stack initialized
I (…) wifi_sta: connected, IP=192.168.1.42  RSSI=-53 dBm
I (…) wifi_sta: online  RSSI=-54 dBm
```

If it logs `connect failed: ESP_FAIL`, the password is wrong or the AP is out of
range; `ESP_ERR_TIMEOUT` means no IP within 15 s. Fix the credentials in
menuconfig and reflash.
