"""Raw-pyserial probe — bypasses pytest to find the serial line state that lets
the board run and answer `@CMD PING`.

Usage (port must be free — no monitor/pytest running):
    python serial_probe.py COM5
"""
import sys
import time

import serial

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM5"
BAUD = 115200


def probe(label, setup):
    """Open the port, apply `setup(ser)`, send PING, report what comes back."""
    ser = serial.Serial(PORT, BAUD, timeout=1)
    try:
        setup(ser)
        time.sleep(0.5)
        ser.reset_input_buffer()
        ser.write(b"@CMD PING\r\n")
        time.sleep(0.6)
        data = ser.read(400)
        ok = b"@RSP PING OK" in data
        print(f"[{label:22}] {'OK  ' if ok else 'noresp'} | {data[:140]!r}")
        return ok
    finally:
        ser.close()


def s_levels(dtr, rts):
    def _setup(ser):
        ser.dtr = dtr
        ser.rts = rts
    return _setup


def s_reset_pulse(ser):
    # Hard-reset into the app: IO0 high (dtr False), pulse EN low via rts.
    ser.dtr = False
    ser.rts = True
    time.sleep(0.1)
    ser.rts = False
    time.sleep(0.5)


print(f"Probing {PORT} @ {BAUD} ...\n")
attempts = [
    ("dtr=0 rts=0", s_levels(False, False)),
    ("dtr=1 rts=1", s_levels(True, True)),
    ("dtr=1 rts=0", s_levels(True, False)),
    ("dtr=0 rts=1", s_levels(False, True)),
    ("reset-pulse", s_reset_pulse),
]
winner = None
for label, setup in attempts:
    try:
        if probe(label, setup) and winner is None:
            winner = label
    except Exception as e:
        print(f"[{label:22}] error: {e}")
    time.sleep(0.4)

print()
print(f"==> working state: {winner}" if winner else
      "==> none answered — the board isn't receiving/responding over raw serial either")
