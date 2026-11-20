"""PC tester for example 33: provision the security secret, then walk the UDS flow.

    python uds_client.py provision --port COM5
    python uds_client.py flow --canable COM8 [--bitrate 250000] [--sink ram|ota]
                              [--image app.bin] [--bad-crc | --corrupt | --expect-refused]
                              [--reset [--expect-version 33-0.2.0]]

provision writes a fresh random 32-byte secret to the board over its USB serial
console and keeps a copy in .uds_secret next to this script (git-ignored). The
secret is never printed. Provisioning is one-time: a board that already holds a secret
answers @PROVISION REFUSED, and provision then stops with exit code 1 and leaves
.uds_secret alone. Physical USB access is the trust boundary here - fine on a
bench, not for a product: see item 1 of the security audit in
docs/SECURITY_HARDENING.md, which must not be skipped.

flow talks UDS through a CANable (slcan) with udsoncan, a UDS client we did not
write, so the board is judged by someone else's implementation:
  extended session -> read DIDs -> security access -> write and read back a DID ->
  store a trouble code, read it, clear it -> programming session -> security access
  -> erase (answered "please wait" first) -> download -> transfer exit -> check ->
  optionally reset, then read back the software version the ECU now runs.
With the OTA sink, --corrupt flips one byte in the middle of the image: the ECU
must refuse it at transfer exit with 0x72 and keep booting the image it has.
--expect-refused sends the image unchanged and expects the same 0x72: for an image the
ECU must not accept, such as one that is not signed by the bench key.
Close Cangaroo first: the CANable's port is exclusive. Exits non-zero on any failure.
"""
import argparse
import hashlib
import hmac
import secrets
import struct
import sys
import time
import zlib
from pathlib import Path

SECRET_FILE = Path(__file__).resolve().parent / ".uds_secret"

DID_VIN, DID_SERIAL, DID_SW, DID_SETTING = 0xF190, 0xF18C, 0xF195, 0x0100
RID_INJECT, RID_ERASE, RID_CHECK = 0x0201, 0xFF00, 0xFF01


# ---- provision -------------------------------------------------------------------

def provision(args):
    import serial

    secret = secrets.token_bytes(32)
    ser = serial.Serial()
    ser.port = args.port
    ser.baudrate = 115200
    ser.timeout = 0.2
    ser.dtr = False          # set before open(): opening must not reset the board
    ser.rts = False
    ser.open()
    try:
        for _ in range(2):   # a second try covers a board that was still booting
            ser.reset_input_buffer()
            ser.write(b"provision " + secret.hex().encode() + b"\r\n")
            buf = b""
            end = time.time() + 3.0
            while time.time() < end:
                buf += ser.read(256)
                if b"@PROVISIONED" in buf:
                    SECRET_FILE.write_text(secret.hex() + "\n")
                    print("provisioned: the board and %s now share a new secret" % SECRET_FILE.name)
                    return 0
                if b"@PROVISION REFUSED" in buf:
                    print("refused: the board already holds a secret, so provisioning is closed.\n"
                          "%s was not changed. Re-provisioning a bench board is an owner action "
                          "(see the README)." % SECRET_FILE.name)
                    return 1
                if b"@PROVISION FAILED" in buf:
                    print("the board refused the secret")
                    return 1
    finally:
        ser.close()
    print("no answer: is example 33 running, with CONFIG_EX33_CONSOLE_PROVISIONING on?")
    return 1


# ---- flow ------------------------------------------------------------------------

class Step:
    """Prints one line per step; any exception fails the run."""

    def __init__(self):
        self.n = 0

    def __call__(self, name, fn):
        self.n += 1
        t0 = time.time()
        result = fn()
        print("%2d. %-44s ok  (%4.0f ms)" % (self.n, name, (time.time() - t0) * 1000))
        return result


def key_algo(level, seed, params):
    """HMAC-SHA256(secret, seed), first len(seed) bytes - must match example 33."""
    return hmac.new(params, bytes(seed), hashlib.sha256).digest()[:len(seed)]


def test_image(n):
    return bytes((i * 7 + 3) & 0xFF for i in range(n))


def read_after_reset(c, did):
    """The ECU is gone while it restarts: wait, then ask until it answers."""
    from udsoncan.exceptions import TimeoutException

    time.sleep(2.5)
    for attempt in range(6):
        try:
            return c.read_data_by_identifier([did])
        except TimeoutException:
            if attempt == 5:
                raise
            time.sleep(0.5)


def flow(args):
    import can
    import isotp
    import udsoncan
    from udsoncan import AsciiCodec, MemoryLocation
    from udsoncan.client import Client
    from udsoncan.connections import PythonIsoTpConnection
    from udsoncan.exceptions import NegativeResponseException
    from udsoncan.services import DiagnosticSessionControl, ECUReset, RoutineControl

    if not SECRET_FILE.exists():
        print("no %s: run 'uds_client.py provision --port COMx' first" % SECRET_FILE.name)
        return 1
    if (args.corrupt or args.expect_refused) and args.sink != "ota":
        print("--corrupt and --expect-refused are for the OTA sink; with the RAM sink use --bad-crc")
        return 1
    if args.corrupt and args.expect_refused:
        print("use --corrupt or --expect-refused, not both")
        return 1
    refuse = args.corrupt or args.expect_refused    # the ECU must answer 0x72 at transfer exit
    secret = bytes.fromhex(SECRET_FILE.read_text().strip())
    print("image: %s" % (args.image or "built-in 4 KB test pattern"))
    print("options: sink=%s bitrate=%d addr=%s corrupt=%s expect-refused=%s bad-crc=%s reset=%s "
          "expect-version=%s" % (args.sink, args.bitrate, args.addr, args.corrupt, args.expect_refused,
                                 args.bad_crc, args.reset, args.expect_version))
    image = Path(args.image).read_bytes() if args.image else test_image(4096)
    if args.corrupt:
        bad = bytearray(image)
        bad[len(bad) // 2] ^= 0xFF    # inside the code: only the image check can catch it
        image = bytes(bad)

    config = dict(udsoncan.configs.default_client_config)
    config["data_identifiers"] = {
        DID_VIN: AsciiCodec(17), DID_SERIAL: AsciiCodec(8), DID_SW: AsciiCodec(8), DID_SETTING: ">I",
    }
    config["security_algo"] = key_algo
    config["security_algo_params"] = secret
    config["server_address_format"] = 32
    config["server_memorysize_format"] = 32
    config["p2_timeout"] = 1.0
    config["p2_star_timeout"] = 6.0
    config["request_timeout"] = args.request_timeout

    bus = can.interface.Bus(interface="slcan", channel=args.canable, bitrate=args.bitrate)
    notifier = can.Notifier(bus, [], timeout=0.1)
    if args.addr == "normfixed":
        address = isotp.Address(isotp.AddressingMode.NormalFixed_29bits, target_address=0x11,
                                source_address=0xF1)
    else:
        address = isotp.Address(isotp.AddressingMode.Normal_11bits, txid=0x7E1, rxid=0x7E9)
    params = {"tx_padding": 0xAA, "blocksize": 8, "stmin": 0, "rx_flowcontrol_timeout": 1000,
              "rx_consecutive_frame_timeout": 1000, "max_frame_size": 4095,
              # udsoncan starts P2 when send() returns: it must return only once the
              # request is on the wire, or a long TransferData times out mid-send.
              "blocking_send": True}
    if args.stmin_override is not None:
        # Measurement only: send faster than the ECU's flow control asks. Not ISO 15765-2.
        params["override_receiver_stmin"] = args.stmin_override / 1000.0
    stack = isotp.NotifierBasedCanStack(bus=bus, notifier=notifier, address=address, params=params)
    step = Step()
    try:
        with Client(PythonIsoTpConnection(stack), config=config) as c:
            r = step("extended session (10 03)",
                     lambda: c.change_session(DiagnosticSessionControl.Session.extendedDiagnosticSession))
            print("      P2 %.0f ms, P2* %.0f ms" % (r.service_data.p2_server_max * 1000,
                                                    r.service_data.p2_star_server_max * 1000))
            r = step("read VIN, serial, version (22)",
                     lambda: c.read_data_by_identifier([DID_VIN, DID_SERIAL, DID_SW]))
            v = r.service_data.values
            print("      VIN %s, serial %s, software %s" % (v[DID_VIN], v[DID_SERIAL], v[DID_SW]))
            step("security access (27 01 / 27 02)", lambda: c.unlock_security_access(1))
            value = int(time.time()) & 0xFFFFFFFF
            step("write setting 0x0100 (2E)", lambda: c.write_data_by_identifier(DID_SETTING, value))
            r = step("read it back (22)", lambda: c.read_data_by_identifier([DID_SETTING]))
            got = r.service_data.values[DID_SETTING]
            got = got[0] if isinstance(got, tuple) else got   # udsoncan struct codecs return a tuple
            print("      wrote %08X, read back %08X" % (value, got))
            assert got == value, "setting did not read back"
            step("store a trouble code (31 01 0201)",
                 lambda: c.routine_control(RID_INJECT, RoutineControl.ControlType.startRoutine))
            r = step("read trouble codes (19 02 FF)", lambda: c.get_dtc_by_status_mask(0xFF))
            codes = ["%06X/%02X" % (d.id, d.status.get_byte_as_int()) for d in r.service_data.dtcs]
            print("      %s" % (", ".join(codes) or "none"))
            assert codes, "expected the stored code"
            step("clear them (14 FFFFFF)", lambda: c.clear_dtc(0xFFFFFF))
            r = step("read again: none left (19 02 FF)", lambda: c.get_dtc_by_status_mask(0xFF))
            assert not r.service_data.dtcs, "codes survived the clear"

            step("programming session (10 02)",
                 lambda: c.change_session(DiagnosticSessionControl.Session.programmingSession))
            step("security access again (a session change locks)", lambda: c.unlock_security_access(1))

            def results_before_start():
                try:
                    c.routine_control(RID_CHECK, RoutineControl.ControlType.requestRoutineResults)
                except NegativeResponseException as e:
                    assert e.response.code == 0x24, "expected 0x24, got 0x%02X" % e.response.code
                else:
                    raise AssertionError("31 03 before any start was answered positively")

            step("check results before a start (31 03 FF01): 0x24", results_before_start)
            t0 = time.time()
            step("erase (31 01 FF00) - waits through 0x78",
                 lambda: c.routine_control(RID_ERASE, RoutineControl.ControlType.startRoutine))
            print("      erase took %.1f s: the server said 'please wait' and udsoncan waited" %
                  (time.time() - t0))
            r = step("request download (34), %d bytes" % len(image),
                     lambda: c.request_download(MemoryLocation(address=0, memorysize=len(image),
                                                               address_format=32, memorysize_format=32)))
            block = r.service_data.max_length - 2
            print("      the ECU takes %d-byte blocks" % block)
            t0 = time.time()
            blocks = [image[i:i + block] for i in range(0, len(image), block)]
            for i, chunk in enumerate(blocks):
                c.transfer_data((i + 1) & 0xFF, chunk)
            secs = time.time() - t0
            step.n += 1
            print("%2d. %-44s ok  (%4.0f ms, %.1f kB/s)" % (step.n, "transfer data (36) x %d" % len(blocks),
                                                          secs * 1000, len(image) / 1024 / max(secs, 1e-3)))
            if refuse:
                try:
                    c.request_transfer_exit()
                except NegativeResponseException as e:
                    if e.response.code != 0x72:
                        raise
                    step.n += 1
                    print("%2d. %-44s ok  (refused with 0x72, as it must be)" %
                          (step.n, "request transfer exit (37), refused image"))
                else:
                    print("FAIL: the ECU accepted an image it must refuse")
                    return 1
            else:
                step("request transfer exit (37)", lambda: c.request_transfer_exit())
            if args.sink == "ram":
                crc = zlib.crc32(image) ^ (1 if args.bad_crc else 0)
                r = step("check (31 01 FF01) against CRC-32 %08X" % crc,
                         lambda: c.routine_control(RID_CHECK, RoutineControl.ControlType.startRoutine,
                                                   data=struct.pack(">I", crc)))
                status = r.service_data.routine_status_record
                ecu_crc = struct.unpack(">I", status[1:5])[0]
                print("      ECU says %s, its CRC-32 %08X" % ("MATCH" if status[0] == 0 else "MISMATCH", ecu_crc))
                if (status[0] == 0) == args.bad_crc:
                    print("FAIL: the check did not give the expected answer")
                    return 1
            else:
                r = step("check (31 01 FF01)",
                         lambda: c.routine_control(RID_CHECK, RoutineControl.ControlType.startRoutine))
                status = r.service_data.routine_status_record
                accepted = status[0] == 0
                print("      image %s" % ("accepted, set to boot" if accepted else "rejected"))
                if accepted == refuse:
                    print("FAIL: the check did not give the expected answer")
                    return 1
            if args.reset:
                step("ECU reset (11 01)", lambda: c.ecu_reset(ECUReset.ResetType.hardReset))
                r = step("after the restart: software version (22 F195)",
                         lambda: read_after_reset(c, DID_SW))
                running = r.service_data.values[DID_SW].strip()
                print("      the ECU now runs %s" % running)
                if args.expect_version and running != args.expect_version:
                    print("FAIL: expected %s" % args.expect_version)
                    return 1
    except Exception as e:   # one line, not a traceback: this is a test tool
        print("FAIL after %d passing steps: %s: %s" % (step.n, type(e).__name__, e))
        return 1
    finally:
        stack.stop() if hasattr(stack, "stop") else None
        notifier.stop()
        bus.shutdown()
    print("all %d steps passed" % step.n)
    return 0


def main():
    ap = argparse.ArgumentParser(description="PC tester for example 33")
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("provision", help="write a fresh security secret over the USB console")
    p.add_argument("--port", required=True, help="the board's USB serial port, e.g. COM5")
    f = sub.add_parser("flow", help="walk the UDS flow through a CANable")
    f.add_argument("--canable", required=True, help="CANable serial port, e.g. COM8")
    f.add_argument("--bitrate", type=int, default=500000)
    f.add_argument("--addr", choices=["norm11", "normfixed"], default="norm11")
    f.add_argument("--sink", choices=["ram", "ota"], default="ram")
    f.add_argument("--image", help="image to download (default: a 4 KB test pattern)")
    f.add_argument("--bad-crc", action="store_true", help="RAM sink: expect the check to fail")
    f.add_argument("--corrupt", action="store_true",
                   help="OTA sink: flip one byte of the image; expect 0x72 at transfer exit")
    f.add_argument("--expect-refused", action="store_true",
                   help="OTA sink: send the image unchanged; expect 0x72 at transfer exit")
    f.add_argument("--reset", action="store_true",
                   help="finish with an ECU reset, then read the version it runs "
                        "(use it between runs: 31 03 FF01 before a start is 0x24 only once per boot)")
    f.add_argument("--expect-version", help="with --reset: the version the ECU must run afterwards")
    f.add_argument("--stmin-override", type=float, metavar="MS",
                   help="measurement only: send frames this many ms apart, ignoring the ECU's STmin")
    f.add_argument("--request-timeout", type=float, default=60.0)
    args = ap.parse_args()
    return provision(args) if args.cmd == "provision" else flow(args)


if __name__ == "__main__":
    sys.exit(main())
