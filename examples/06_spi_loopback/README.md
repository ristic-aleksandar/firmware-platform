# 06_spi_loopback

Full-duplex SPI transfer, checked against a **MOSI→MISO** loopback jumper. Proves
`e32p_spi` bus + device + transfer. The transfer always runs (driver-level proof
on a bare board); the byte-for-byte match is `PASS` only with the jumper.

## Build & flash

```powershell
. C:\Espressif\Initialize-Idf.ps1 -IdfId esp-idf-ab7213b7273352b64422b1f400ff27a0
# From the repository root
idf.py -C examples\06_spi_loopback set-target esp32
idf.py -C examples\06_spi_loopback -p COM5 build flash monitor
```

Without a jumper (bare board):

```
I (…) spi_loop: SPI2 ready (sclk=18 mosi=23 miso=19 cs=21) - jumper MOSI->MISO for PASS
I (…) spi_loop: transfer ok  tx=e32a0055 rx=00000000  [no jumper (rx undefined)]
```

With a jumper wire from **GPIO23 (MOSI) to GPIO19 (MISO)**:

```
I (…) spi_loop: transfer ok  tx=e32a0155 rx=e32a0155  [PASS (loopback)]
```

`transfer ok` proves the driver path works; `PASS` proves MOSI data clocks back
in on MISO byte-for-byte. To talk to a real device instead, remove the jumper
and wire SCLK/MOSI/MISO/CS to the peripheral.
