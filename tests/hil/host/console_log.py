"""Log a HIL board's console without resetting it, and pull out any crash.

Opening a serial port normally pulses DTR and RTS, and on these DevKits that resets
the board - the last thing you want while watching for a crash. This tool sets both
lines low *before* the port opens, so the board keeps running untouched.

    python console_log.py COM7                          # until Ctrl+C
    python console_log.py COM7 --seconds 60 --out companion.log
    python console_log.py COM7 --elf ../agent/build_esp32s3/hil_agent.elf

Every line is timestamped. At the end it lists panic and reset lines and any
backtraces, with the addr2line command that decodes them against the image that was
running. It only reads: nothing is ever written to the board.
"""
import argparse
import datetime
import re
import sys
import time

import serial

# Lines worth surfacing: panics, watchdogs, brownouts, and ROM reset banners.
ALARM = re.compile(r"Guru Meditation|Interrupt wdt timeout|Task watchdog|abort\(\)|"
                   r"Brownout|rst:0x[0-9a-f]+")
BACKTRACE = re.compile(r"Backtrace:((?:\s+0x[0-9a-fA-F]+:0x[0-9a-fA-F]+)+)")

ADDR2LINE = {"esp32": "xtensa-esp32-elf-addr2line", "esp32s3": "xtensa-esp32s3-elf-addr2line"}


def open_quietly(port, baud):
    """Open the port with DTR and RTS already low, so the board is not reset."""
    ser = serial.Serial()
    ser.port = port
    ser.baudrate = baud
    ser.timeout = 0.2
    ser.dtr = False      # set before open(): pyserial applies both as the port
    ser.rts = False      # opens, so the reset pulse never happens
    ser.open()
    return ser


def main():
    ap = argparse.ArgumentParser(description="Log a HIL board's console without resetting it.")
    ap.add_argument("port", help="serial port, e.g. COM7")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--seconds", type=float, default=0.0, help="stop after this long (0 = until Ctrl+C)")
    ap.add_argument("--out", help="also write the timestamped log to this file")
    ap.add_argument("--elf", help="image that was running, to build the addr2line command")
    ap.add_argument("--chip", choices=sorted(ADDR2LINE), default="esp32s3",
                    help="selects the addr2line binary (default esp32s3, the companion)")
    args = ap.parse_args()

    ser = open_quietly(args.port, args.baud)
    out = open(args.out, "w", encoding="utf-8") if args.out else None
    alarms, traces = [], []
    pending = b""
    end = time.time() + args.seconds if args.seconds > 0 else None
    print("logging %s at %d baud - Ctrl+C to stop" % (args.port, args.baud), file=sys.stderr)

    try:
        while end is None or time.time() < end:
            pending += ser.read(4096)
            while b"\n" in pending:
                raw, pending = pending.split(b"\n", 1)
                line = raw.decode(errors="replace").rstrip("\r")
                stamped = "%s  %s" % (datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3], line)
                print(stamped)
                if out:
                    out.write(stamped + "\n")
                if ALARM.search(line):
                    alarms.append(stamped)
                m = BACKTRACE.search(line)
                if m:
                    traces.append(m.group(1).split())
    except KeyboardInterrupt:
        pass
    finally:
        ser.close()
        if out:
            out.close()

    print("\n--- %d alarm line(s), %d backtrace(s)" % (len(alarms), len(traces)), file=sys.stderr)
    for a in alarms[:20]:
        print("   " + a, file=sys.stderr)
    for t in traces[:3]:
        pcs = " ".join(frame.split(":")[0] for frame in t)
        print("\n   decode with:\n   %s -pfiaC -e %s %s"
              % (ADDR2LINE[args.chip], args.elf or "<path to the running .elf>", pcs), file=sys.stderr)


if __name__ == "__main__":
    main()
