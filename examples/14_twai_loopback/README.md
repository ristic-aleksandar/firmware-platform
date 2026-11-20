# 14_twai_loopback

Transmits a CAN frame to itself using TWAI NO_ACK mode + self-reception. Proves
`e32p_twai`. Unlike UART, **TWAI does not loop back internally** — receiving the
frame needs a wire from TX (GPIO21) to RX (GPIO22), or an external transceiver.

## Build & flash

```powershell
. C:\Espressif\Initialize-Idf.ps1 -IdfId esp-idf-ab7213b7273352b64422b1f400ff27a0
idf.py -C C:\Users\aleks\Work\esp32_platform\examples\14_twai_loopback set-target esp32
idf.py -C C:\Users\aleks\Work\esp32_platform\examples\14_twai_loopback -p COM5 build flash monitor
```

### Bare board (TX path only)

```
I (…) e32p_twai: twai installed tx=21 rx=22 bitrate=500000 mode=1
I (…) twai: TWAI NO_ACK @500k tx=21 rx=22 - jumper GPIO21->GPIO22 (or a transceiver) for RX
W (…) twai: tx=ESP_OK rx=ESP_ERR_TIMEOUT  (jumper GPIO21->GPIO22 or add a transceiver)
```

The frame is queued and sent (`tx=ESP_OK`); with nothing looping it back, `rx`
times out — expected.

### With a jumper GPIO21 → GPIO22

```
I (…) twai: sent id=0x123 -> got id=0x123 data=dead05ef [PASS]
```

The frame comes back byte-identical. For a real CAN bus, remove the jumper and
wire TX/RX through a transceiver (SN65HVD230 / TJA1050) to your bus.
