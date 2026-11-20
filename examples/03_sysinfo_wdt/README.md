# 03_sysinfo_wdt

Dumps chip/app identity, the last reset reason, and heap, then runs a
watchdog-fed heartbeat. Proves the rest of the M2 spine: `e32p_sysinfo` and
`e32p_wdt`.

## Build & flash

```powershell
. C:\Espressif\Initialize-Idf.ps1 -IdfId esp-idf-ab7213b7273352b64422b1f400ff27a0
idf.py -C C:\Users\aleks\Work\esp32_platform\examples\03_sysinfo_wdt set-target esp32
idf.py -C C:\Users\aleks\Work\esp32_platform\examples\03_sysinfo_wdt -p COM5 build flash monitor
```

Expected at boot:

```
I (…) e32p_sysinfo: chip=ESP32 cores=2 rev=1 flash=4MB
I (…) e32p_sysinfo: MAC=0c:b8:15:c3:83:54
I (…) e32p_sysinfo: app=sysinfo_wdt ver=1 idf=v5.3.1-dirty
I (…) e32p_sysinfo: reset=power-on free_heap=… min_free=…
I (…) sysinfo_wdt: reset reason: power-on
I (…) e32p_wdt: task WDT set: timeout=3000ms panic=1
I (…) sysinfo_wdt: alive #0  uptime=…ms  free_heap=…
```

## Things to try

- **Reset reason** — power-cycle vs. pressing **EN** vs. a crash all change the
  `reset reason:` line.
- **Trigger the watchdog** — set `FEED_THE_DOG` to `0` in `main/main.c`, reflash.
  After ~3 s the board panics and reboots, and the next boot reports
  `reset reason: task watchdog`. Set it back to `1` to recover.
