"""HIL ISO-TP interop through a CANable - slice 1.7 of docs/tickets/diagnostics-stack.md.

These tests put a PC CAN adapter (a CANable running slcan firmware) on the HIL bus:

- test_canable_sniffs_ff_fc_cf - the CANable records a 100-byte exchange between
  the DUT and the companion, and checks every frame of both directions against
  ISO 15765-2 on its own: First Frame length, flow control, sequence numbers,
  padding, and the payload bytes themselves.
- test_canable_isotp_peer - the PC is the tester, using can-isotp (an ISO-TP
  implementation we did not write) against the companion's echo peer.

Close Cangaroo, or any other tool holding the CANable's port, first:
    pytest test_isotp_canable.py --port COM5 --companion-port COM7 --canable COM8

Needs python-can and can-isotp (see requirements.txt). Skipped without --canable.
Each test opens the adapter and closes it again, so it is never on the bus during
the rest of the suite.

Both tests open the adapter in normal mode. This CANable's slcan firmware passes no
frames through when opened listen-only (python-can sends 'L'): on 2026-09-15 the same
transfer gave 0 frames listen-only and 34 in normal mode. In normal mode the adapter
acknowledges frames but sends none of its own; the sniff test checks frame content,
which an acknowledgement cannot fake. candleLight (gs_usb) firmware would allow a
truly passive capture.
"""
import threading
import time

import pytest

can = pytest.importorskip("can")
isotp = pytest.importorskip("isotp")

ECU_ECHO = 0x12       # the companion's echo peer (tests/hil/agent/main/hil_diag.c)
TESTER_SA = 0xF1
PAD = 0xAA            # both boards pad every ISO-TP frame to 8 bytes with 0xAA


def _ids(addr):
    """(tester->ECU id, ECU->tester id, extended) for the companion's echo channel."""
    if addr == "normfixed":
        return (0x18DA0000 | (ECU_ECHO << 8) | TESTER_SA,
                0x18DA0000 | (TESTER_SA << 8) | ECU_ECHO, True)
    return 0x7E0 + (ECU_ECHO - 0x10), 0x7E8 + (ECU_ECHO - 0x10), False


def _expected_payload(length, seed):
    """What hil_diag.c:echo_xfer sends: byte i = 7i + seed."""
    return bytes((i * 7 + seed) & 0xFF for i in range(length))


def _check_transfer(frames, src, dst, length, expect):
    """Walk one segmented transfer from `src` in the captured frames and check it
    frame by frame. Frames from `dst` inside the transfer must be Flow Control."""
    start = next((i for i, f in enumerate(frames)
                  if f.arbitration_id == src and (f.data[0] >> 4) == 0x1), None)
    assert start is not None, f"no First Frame from 0x{src:X} in {len(frames)} captured frames"

    ff = frames[start]
    assert ff.dlc == 8, f"First Frame DLC {ff.dlc}"
    ff_len = ((ff.data[0] & 0x0F) << 8) | ff.data[1]
    assert ff_len == length, f"First Frame announces {ff_len} bytes, expected {length}"

    data = bytearray(ff.data[2:8])
    expected_sn = 1
    cfs = fcs = 0
    block = None
    for f in frames[start + 1:]:
        if len(data) >= length:
            break
        pci = f.data[0] >> 4
        if f.arbitration_id == dst:
            assert pci == 0x3, f"frame from the receiver inside the transfer is not Flow Control: {f}"
            assert (f.data[0] & 0x0F) == 0, f"Flow Control is not CTS: {f}"
            assert f.dlc == 8 and all(b == PAD for b in f.data[3:8]), f"Flow Control not padded with 0x{PAD:02X}: {f}"
            block = f.data[1]
            fcs += 1
        elif f.arbitration_id == src:
            assert pci == 0x2, f"frame from the sender inside the transfer is not a Consecutive Frame: {f}"
            sn = f.data[0] & 0x0F
            assert sn == expected_sn, f"sequence number {sn}, expected {expected_sn}"
            expected_sn = (expected_sn + 1) % 16
            data += f.data[1:8]
            cfs += 1
    assert len(data) >= length, f"transfer incomplete: {len(data)} of {length} bytes captured"

    # Padding on the final Consecutive Frame: everything after the last payload byte.
    used_in_last = length - 6 - 7 * (cfs - 1)
    tail = data[len(data) - (7 - used_in_last):] if used_in_last < 7 else b""
    assert all(b == PAD for b in tail), f"final Consecutive Frame not padded with 0x{PAD:02X}"

    assert bytes(data[:length]) == expect, "reassembled bytes differ from what the sender sent"
    assert block is not None, "no Flow Control seen"
    expected_fcs = 1 + (cfs - 1) // block if block else 1
    assert fcs == expected_fcs, f"{fcs} Flow Control frames for {cfs} CFs at block size {block}, expected {expected_fcs}"
    return cfs, fcs


@pytest.mark.two_board
@pytest.mark.needs_xcvr
@pytest.mark.needs_canable
def test_canable_sniffs_ff_fc_cf(canable, companion, agent, can_build):
    # canable first: without --canable the test skips before any port is opened.
    rate, addr = can_build
    req_id, resp_id, ext = _ids(addr)
    # Normal mode, not listen-only: this adapter's slcan firmware delivers no frames
    # when opened with 'L' (see the module docstring). It acknowledges but never
    # transmits here, and everything asserted below is frame content.
    bus = can.interface.Bus(interface="slcan", channel=canable, bitrate=rate)
    frames = []
    stop = threading.Event()

    def reader():
        while not stop.is_set():
            msg = bus.recv(timeout=0.1)
            if msg is not None and msg.is_extended_id == ext and msg.arbitration_id in (req_id, resp_id):
                frames.append(msg)

    t = threading.Thread(target=reader, daemon=True)
    t.start()
    try:
        time.sleep(0.3)                          # adapter settles before traffic starts
        companion.flush()
        agent.flush()
        agent.cmd("RUN isotp_mf")
        agent.expect(rb"@RSP RUN isotp_mf PASS", timeout=10)
        time.sleep(0.3)                          # the last frames reach the adapter
    finally:
        stop.set()
        t.join(timeout=2)
        bus.shutdown()

    payload = _expected_payload(100, ECU_ECHO)
    _check_transfer(frames, src=req_id, dst=resp_id, length=100, expect=payload)   # DUT request
    _check_transfer(frames, src=resp_id, dst=req_id, length=100, expect=payload)   # companion echo


@pytest.mark.needs_xcvr
@pytest.mark.needs_canable
def test_canable_isotp_peer(canable, companion, can_build):
    rate, addr = can_build
    # Normal mode: here the PC is a real node on the bus, and the companion must
    # talk ISO-TP with an implementation that is not ours.
    bus = can.interface.Bus(interface="slcan", channel=canable, bitrate=rate)
    notifier = can.Notifier(bus, [], timeout=0.1)
    if addr == "normfixed":
        address = isotp.Address(isotp.AddressingMode.NormalFixed_29bits,
                                target_address=ECU_ECHO, source_address=TESTER_SA)
    else:
        req_id, resp_id, _ = _ids(addr)
        address = isotp.Address(isotp.AddressingMode.Normal_11bits, txid=req_id, rxid=resp_id)
    params = {
        "stmin": 1, "blocksize": 8, "tx_padding": PAD,
        "rx_flowcontrol_timeout": 1000, "rx_consecutive_frame_timeout": 1000,
        "blocking_send": True,
    }
    stack = isotp.NotifierBasedCanStack(bus=bus, notifier=notifier, address=address, params=params)
    payload = bytes((i * 11 + 5) & 0xFF for i in range(100))
    echo = None
    try:
        stack.start()
        companion.flush()
        stack.send(payload, send_timeout=5)
        echo = stack.recv(block=True, timeout=5)
    finally:
        stack.stop()
        notifier.stop()
        bus.shutdown()

    # The companion independently reports what it reassembled.
    companion.expect(rb"@EVT ISOTP rx ch=echo len=100 echo=ok", timeout=3)
    assert echo is not None, "no echo from the companion within 5 s"
    assert bytes(echo) == payload, "echo differs from what the PC sent"
