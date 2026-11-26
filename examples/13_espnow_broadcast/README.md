# 13_espnow_broadcast

Broadcasts a message every 2 s and logs any received ones. Proves `e32p_espnow`
(peer-to-peer over Wi-Fi, no router).

## Build & flash

```powershell
. C:\Espressif\Initialize-Idf.ps1 -IdfId esp-idf-ab7213b7273352b64422b1f400ff27a0
# From the repository root
idf.py -C examples\13_espnow_broadcast set-target esp32
idf.py -C examples\13_espnow_broadcast -p COM5 build flash monitor
```

### One board (TX path)

```
I (…) e32p_espnow: esp-now ready
I (…) espnow: this board MAC = 0c:b8:15:c3:83:54 - broadcasting (flash a 2nd board to see RX)
I (…) espnow: broadcast "e32p hello #0" [sent]
I (…) espnow: broadcast "e32p hello #1" [sent]
```

`[sent]` confirms the send path. A board does **not** receive its own
broadcasts, so there is no `RX` line with a single board.

### Two boards (full exchange)

Flash the same firmware to a second ESP32. Each board then logs the other's
messages:

```
I (…) espnow: RX from 24:6f:28:aa:bb:cc : "e32p hello #7"
```

That's real board-to-board messaging with no access point involved.
