# 04_uart_loopback

Sends a message on UART1 and reads it back through the on-chip loopback — a
self-test that needs **no wiring**. Proves the M3 bus pattern: `e32p_uart`'s
opaque-handle-per-port lifecycle and real TX/RX.

## Build & flash

```powershell
. C:\Espressif\Initialize-Idf.ps1 -IdfId esp-idf-ab7213b7273352b64422b1f400ff27a0
# From the repository root
idf.py -C examples\04_uart_loopback set-target esp32
idf.py -C examples\04_uart_loopback -p COM5 build flash monitor
```

Expected (repeats once per second):

```
I (…) uart_loop: UART1 loopback ready (tx=17 rx=16)
I (…) uart_loop: sent "e32p uart #0" (12) -> got "e32p uart #0" (12)  [PASS]
I (…) uart_loop: sent "e32p uart #1" (12) -> got "e32p uart #1" (12)  [PASS]
```

`PASS` means the bytes written came back byte-identical through the UART
hardware. To talk to a real device instead, remove the `set_loopback` call and
wire TX/RX to the peer.
