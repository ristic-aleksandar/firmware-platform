# 05_i2c_scan

Scans the I2C bus every 2 s and lists any devices found. Proves `e32p_i2c`
bus init + probe/scan. Self-contained: on a bare board it reports "no devices"
cleanly; wire a sensor and its address shows up.

## Build & flash

```powershell
. C:\Espressif\Initialize-Idf.ps1 -IdfId esp-idf-ab7213b7273352b64422b1f400ff27a0
idf.py -C C:\Users\aleks\Work\esp32_platform\examples\05_i2c_scan set-target esp32
idf.py -C C:\Users\aleks\Work\esp32_platform\examples\05_i2c_scan -p COM5 build flash monitor
```

Expected on a bare board:

```
I (…) i2c_scan: scanning I2C0 on SDA=21 SCL=22 ...
I (…) i2c_scan: no I2C devices found (wire a sensor to SDA=21 SCL=22)
```

With a device wired (e.g. a BMP280 at 0x76):

```
I (…) i2c_scan: found 1 device(s): 0x76
```

## Wiring note

Connect the sensor's SDA to GPIO21 and SCL to GPIO22, plus 3V3 and GND. Add
external 2.2–4.7 kΩ pull-ups to 3V3 if your breakout board doesn't have them —
the ESP32's internal pull-ups are weak.
