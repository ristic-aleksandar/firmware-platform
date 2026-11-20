"""HIL UDS interop through a CANable - slice 3.7 of docs/tickets/diagnostics-stack.md.

Here the PC is the tester, with udsoncan - a UDS client we did not write - over
can-isotp and python-can. So the companion's UDS server is judged by someone else's
reading of ISO 14229-1, not by our own tester:

    pytest test_uds_canable.py --port COM5 --companion-port COM7 --canable COM8

Two tests. The first walks the whole flow: sessions, DIDs, security access, trouble
codes, erase, download, check, and an ECU reset that really restarts the companion.
The second proves the "please wait" rule (D-16) the strict way: udsoncan uses the
P2 = 50 ms and P2* = 5 s the server announces, and the erase is told to take 7 s.
udsoncan only gets through it if the first 0x78 comes inside P2 and the server
repeats it inside P2*; otherwise udsoncan itself raises a timeout.

Security access uses the HIL TEST SECRET, the same bytes as UDS_TEST_SECRET in
agent/main/hil_diag.c - a labelled test fixture, not a key (item 4 of the security
audit in docs/SECURITY_HARDENING.md).

Close Cangaroo first: the adapter's port is exclusive. Each test opens the adapter
and closes it again, so it is never on the bus during the rest of the suite.
Skipped without --canable.
"""
import hashlib
import hmac
import os
import struct
import time
import zlib

import pytest

can = pytest.importorskip("can")
isotp = pytest.importorskip("isotp")
udsoncan = pytest.importorskip("udsoncan")

from udsoncan import AsciiCodec, Dtc, MemoryLocation  # noqa: E402
from udsoncan.client import Client  # noqa: E402
from udsoncan.connections import PythonIsoTpConnection  # noqa: E402
from udsoncan.services import DiagnosticSessionControl, ECUReset, RoutineControl  # noqa: E402

ECU_UDS = 0x11              # the companion's UDS server
TESTER_SA = 0xF1
PHYS_REQ_11 = 0x7E1
RESP_11 = 0x7E9
RESP_29 = 0x18DA0000 | (TESTER_SA << 8) | ECU_UDS

HIL_TEST_SECRET = b"HIL TEST SECRET - NOT A REAL KEY"
VIN = "E32PHIL0000000001"   # the same VIN the companion's OBD-II server reports
DID_VIN, DID_SETTING = 0xF190, 0x0100
SETTING_BOOT = 1            # 0x0100 is kept in RAM: this after every restart
RID_INJECT, RID_ERASE, RID_CHECK = 0x0201, 0xFF00, 0xFF01
P0217 = 0x021700            # ISO 15031-6 code + failure type byte 00
CONFIRMED = 0x08
LONG_ERASE_S = 7            # longer than P2*: the server must repeat its 0x78
P2_STAR_S = 5.0
REPEAT_S = 0.8 * P2_STAR_S  # when the server sends its next 0x78

Session = DiagnosticSessionControl.Session
START = RoutineControl.ControlType.startRoutine

PARAMS = {
    "tx_padding": 0xAA, "blocksize": 8, "stmin": 0, "max_frame_size": 4095,
    "rx_flowcontrol_timeout": 1000, "rx_consecutive_frame_timeout": 1000,
    # udsoncan starts P2 when send() returns: it must return only once the request
    # is on the wire, or a 1026-byte TransferData runs out of P2 while still sending.
    "blocking_send": True,
}


def _key(level, seed, params):
    """key = HMAC-SHA256(secret, seed), first len(seed) bytes - as the server checks it."""
    return hmac.new(params, bytes(seed), hashlib.sha256).digest()[:len(seed)]


def _config():
    cfg = dict(udsoncan.configs.default_client_config)
    cfg["data_identifiers"] = {DID_VIN: AsciiCodec(17), DID_SETTING: ">I"}
    cfg["security_algo"] = _key
    cfg["security_algo_params"] = HIL_TEST_SECRET
    cfg["server_address_format"] = 32
    cfg["server_memorysize_format"] = 32
    # After the first 10 xx, udsoncan times every answer with the P2 and P2* the
    # server announced in it - our own numbers, held to by someone else's client.
    cfg["use_server_timing"] = True
    cfg["request_timeout"] = 20.0      # the whole request, 0x78 waits included
    return cfg


class _PendingWatch(can.Listener):
    """Notes the arrival time of each 7F 31 78 the server sends."""

    def __init__(self, resp_id):
        self.resp_id = resp_id
        self.times = []

    def on_message_received(self, msg):
        if msg.arbitration_id == self.resp_id and bytes(msg.data[:4]) == b"\x03\x7F\x31\x78":
            self.times.append(time.perf_counter())


class _Tester:
    """The PC as a UDS tester: one adapter, one ISO-TP stack, one udsoncan client."""

    def __init__(self, channel, rate, addr):
        if addr == "normfixed":
            address = isotp.Address(isotp.AddressingMode.NormalFixed_29bits,
                                    target_address=ECU_UDS, source_address=TESTER_SA)
            resp_id = RESP_29
        else:
            address = isotp.Address(isotp.AddressingMode.Normal_11bits,
                                    txid=PHYS_REQ_11, rxid=RESP_11)
            resp_id = RESP_11
        self.bus = can.interface.Bus(interface="slcan", channel=channel, bitrate=rate)
        self.watch = _PendingWatch(resp_id)
        self.notifier = can.Notifier(self.bus, [self.watch], timeout=0.1)
        self.stack = isotp.NotifierBasedCanStack(bus=self.bus, notifier=self.notifier,
                                                 address=address, params=PARAMS)
        self.client = Client(PythonIsoTpConnection(self.stack), config=_config())
        self.client.open()

    def close(self):
        try:
            self.client.close()
        finally:
            self.notifier.stop()
            self.bus.shutdown()


def _setting(c):
    got = c.read_data_by_identifier([DID_SETTING]).service_data.values[DID_SETTING]
    return got[0] if isinstance(got, tuple) else got   # struct codecs return a tuple


@pytest.mark.two_board
@pytest.mark.needs_xcvr
@pytest.mark.needs_canable
def test_uds_canable_full_flow(canable, companion, can_build):
    # canable first: without --canable the test skips before any port is opened.
    rate, addr = can_build
    image = bytes((i * 7 + 3) & 0xFF for i in range(4096))
    value = int.from_bytes(os.urandom(4), "big") | 0x80000000   # never the boot value
    t = _Tester(canable, rate, addr)
    try:
        c = t.client
        c.change_session(Session.extendedDiagnosticSession)
        vin = c.read_data_by_identifier([DID_VIN]).service_data.values[DID_VIN]
        assert vin == VIN, vin                     # 20 bytes: a multi-frame answer

        c.unlock_security_access(1)
        c.write_data_by_identifier(DID_SETTING, value)
        assert _setting(c) == value

        # One store, two views: the routine stores P0217 in the OBD-II store.
        c.routine_control(RID_INJECT, START)
        n = c.get_number_of_dtc_by_status_mask(0xFF).service_data
        assert (n.dtc_count, n.dtc_format) == (1, Dtc.Format.ISO15031_6), (n.dtc_count, n.dtc_format)
        dtcs = c.get_dtc_by_status_mask(0xFF).service_data.dtcs
        assert [(d.id, d.status.get_byte_as_int()) for d in dtcs] == [(P0217, CONFIRMED)]
        c.clear_dtc(0xFFFFFF)
        assert not c.get_dtc_by_status_mask(0xFF).service_data.dtcs

        c.change_session(Session.programmingSession)
        c.unlock_security_access(1)               # a session change locked it again
        c.routine_control(RID_ERASE, START)       # 2 s, behind one 0x78
        r = c.request_download(MemoryLocation(address=0, memorysize=len(image),
                                              address_format=32, memorysize_format=32))
        block = r.service_data.max_length - 2
        assert block == 1024, r.service_data.max_length
        for i, off in enumerate(range(0, len(image), block)):
            c.transfer_data((i + 1) & 0xFF, image[off:off + block])
        c.request_transfer_exit()
        crc = zlib.crc32(image)
        for wrong in (0, 1):                      # 00 match, 01 mismatch; then the ECU's CRC
            st = c.routine_control(RID_CHECK, START, data=struct.pack(">I", crc ^ wrong))
            assert st.service_data.routine_status_record == bytes([wrong]) + struct.pack(">I", crc)

        companion.flush()
        c.ecu_reset(ECUReset.ResetType.hardReset)
        companion.expect(b"@EVT UDS server ready", timeout=10)
        assert _setting(c) == SETTING_BOOT        # only a real restart explains this
    finally:
        t.close()


@pytest.mark.two_board
@pytest.mark.needs_xcvr
@pytest.mark.needs_canable
def test_uds_canable_follows_response_pending(canable, companion, can_build):
    rate, addr = can_build
    t = _Tester(canable, rate, addr)
    try:
        c = t.client
        c.change_session(Session.programmingSession)
        timing = c.get_session_timing()
        assert (timing.p2_server_max, timing.p2_star_server_max) == (0.05, P2_STAR_S), timing
        c.unlock_security_access(1)
        t.watch.times.clear()
        t0 = time.perf_counter()
        r = c.routine_control(RID_ERASE, START, data=bytes([LONG_ERASE_S]))
        elapsed = time.perf_counter() - t0
        pending = [x - t0 for x in t.watch.times]
        c.change_session(Session.defaultSession)
    finally:
        t.close()

    assert r.service_data.routine_status_record == b"\x00"
    # udsoncan restarts its 5 s P2* wait at each 0x78. A 7 s job therefore only
    # succeeds if the server repeated the 0x78 before that wait ran out.
    assert LONG_ERASE_S - 0.2 < elapsed < LONG_ERASE_S + 1.0, elapsed
    assert len(pending) == 2, f"0x78 seen at {pending} s"
    assert pending[0] < 0.1, f"first 0x78 after {pending[0]:.3f} s"
    gap = pending[1] - pending[0]
    assert abs(gap - REPEAT_S) < 0.1, f"0x78 repeated after {gap:.3f} s, want {REPEAT_S} s"
    print(f"\n0x78 at {pending[0] * 1000:.1f} ms and {pending[1] * 1000:.1f} ms, "
          f"result at {elapsed * 1000:.1f} ms")
