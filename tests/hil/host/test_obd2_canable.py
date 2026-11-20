"""HIL OBD-II interop through a CANable - slice 2.5 of docs/tickets/diagnostics-stack.md.

Here the PC is the scan tool. python-can drives the CANable and can-isotp - an
ISO-TP implementation we did not write - carries the requests, so the companion's
OBD-II server is judged end to end by someone else's transport:

    pytest test_obd2_canable.py --port COM5 --companion-port COM7 --canable COM8

Close Cangaroo first: the adapter's port is exclusive. Each test opens the adapter
and closes it again, so it is never on the bus during the rest of the suite. Normal
mode, not listen-only - this CANable's slcan firmware passes no frames at all when
opened listen-only (see test_isotp_canable.py).

The server's values are fixed (hil_diag.c): 1726 rpm, 88 km/h, VIN
E32PHIL0000000001, and PID 0x42 unsupported. Skipped without --canable.
"""
import pytest

can = pytest.importorskip("can")
isotp = pytest.importorskip("isotp")

ECU_OBD = 0x10              # the companion's OBD-II server
TESTER_SA = 0xF1
PHYS_REQ_11 = 0x7E0
RESP_11 = 0x7E8
FUNC_REQ_11 = 0x7DF
PHYS_REQ_29 = 0x18DA0000 | (ECU_OBD << 8) | TESTER_SA
FUNC_REQ_29 = 0x18DB33F1
RESP_29 = 0x18DA0000 | (TESTER_SA << 8) | ECU_OBD

PAD = 0xAA                  # both boards pad every ISO-TP frame to 8 bytes
VIN = b"E32PHIL0000000001"
SILENCE_S = 1.0             # how long a broadcast must stay unanswered (N_Bs)

PARAMS = {
    "stmin": 1, "blocksize": 8, "tx_padding": PAD,
    "rx_flowcontrol_timeout": 1000, "rx_consecutive_frame_timeout": 1000,
    "blocking_send": True,
}


def _address(addr, functional):
    """The tester's ISO-TP address for this build's addressing mode.

    A functional request goes out on the broadcast ID but the answer comes back on
    the ECU's own response ID (decision D-05), so the two directions are not
    symmetric and the 29-bit functional case cannot use NormalFixed."""
    if addr == "normfixed":
        if functional:
            return isotp.Address(isotp.AddressingMode.Normal_29bits,
                                 txid=FUNC_REQ_29, rxid=RESP_29)
        return isotp.Address(isotp.AddressingMode.NormalFixed_29bits,
                             target_address=ECU_OBD, source_address=TESTER_SA)
    return isotp.Address(isotp.AddressingMode.Normal_11bits,
                         txid=FUNC_REQ_11 if functional else PHYS_REQ_11, rxid=RESP_11)


class _Tester:
    """The PC as an OBD-II tester on the bus: one adapter, one ISO-TP stack."""

    def __init__(self, channel, rate, addr, functional=False):
        self.bus = can.interface.Bus(interface="slcan", channel=channel, bitrate=rate)
        self.notifier = can.Notifier(self.bus, [], timeout=0.1)
        self.stack = isotp.NotifierBasedCanStack(
            bus=self.bus, notifier=self.notifier,
            address=_address(addr, functional), params=PARAMS)
        self.stack.start()

    def ask(self, request, timeout=5.0):
        """Send one request; return the response, or None if nothing came back."""
        self.stack.send(bytes(request), send_timeout=5)
        got = self.stack.recv(block=True, timeout=timeout)
        return None if got is None else bytes(got)

    def close(self):
        self.stack.stop()
        self.notifier.stop()
        self.bus.shutdown()


@pytest.mark.two_board
@pytest.mark.needs_xcvr
@pytest.mark.needs_canable
def test_obd2_canable_pid_and_vin(canable, companion, can_build):
    # canable first: without --canable the test skips before any port is opened.
    rate, addr = can_build
    t = _Tester(canable, rate, addr)
    try:
        rpm = t.ask([0x01, 0x0C])
        vin = t.ask([0x09, 0x02])
    finally:
        t.close()

    assert rpm == b"\x41\x0C\x1A\xF8", f"rpm answer was {rpm!r}"     # 1726 rpm
    # 20 bytes: the response does not fit one frame, so can-isotp reassembled a
    # First Frame, sent its own Flow Control, and collected the Consecutive Frames.
    assert vin == b"\x49\x02\x01" + VIN, f"vin answer was {vin!r}"


@pytest.mark.two_board
@pytest.mark.needs_xcvr
@pytest.mark.needs_canable
def test_obd2_canable_broadcast_and_silence(canable, companion, can_build):
    rate, addr = can_build

    # Physically first, with a PID the server does not support: a negative answer
    # proves the path is alive, which is what makes the silence below mean
    # something other than a dead ECU.
    t = _Tester(canable, rate, addr)
    try:
        negative = t.ask([0x01, 0x42])
    finally:
        t.close()
    assert negative == b"\x7F\x01\x12", f"physical answer was {negative!r}"

    t = _Tester(canable, rate, addr, functional=True)
    try:
        speed = t.ask([0x01, 0x0D])
        quiet = t.ask([0x01, 0x42], timeout=SILENCE_S)
    finally:
        t.close()

    # Broadcast request, answered from the ECU's own response ID (D-05).
    assert speed == b"\x41\x0D\x58", f"broadcast answer was {speed!r}"   # 88 km/h
    # An unsupported PID asked functionally must draw nothing at all (D-06).
    assert quiet is None, f"broadcast of an unsupported PID drew {quiet!r}"
