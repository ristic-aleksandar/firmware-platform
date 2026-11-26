# 02_nvs_boot_count

Persists a boot counter in NVS and dispatches it through the default event loop.
Proves the M2 substrate: `e32p_nvs` (the count survives resets) and
`e32p_event` (a custom event is posted and handled).

## Build & flash

```powershell
. C:\Espressif\Initialize-Idf.ps1 -IdfId esp-idf-ab7213b7273352b64422b1f400ff27a0
# From the repository root
idf.py -C examples\02_nvs_boot_count set-target esp32
idf.py -C examples\02_nvs_boot_count -p COM5 build flash monitor
```

Expected — the count increments by one on every reset:

```
I (…) boot_count: boot count = 3
I (…) boot_count: event received: boot #3
```

Reset the board (EN button) and watch it go to 4, 5, … — that persistence is
the proof NVS is working. `idf.py -p COM5 erase-flash` resets it to 1.
