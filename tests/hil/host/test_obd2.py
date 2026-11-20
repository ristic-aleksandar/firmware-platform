"""HIL OBD-II (SAE J1979 / ISO 15765-4) tests - slice 2.4 of docs/tickets/diagnostics-stack.md.

The COMPANION runs an OBD-II server (ECU 0x10: requests on 0x7E0, broadcast on
0x7DF, answers on 0x7E8) with fixed values - 90 degC, 1726 rpm, 88 km/h, VIN
E32PHIL0000000001 - so the DUT's checks are exact. The DUT plays the scan tool:

    pytest test_obd2.py --port COM5 --companion-port COM7

OBD-II has no service that stores a trouble code, so the DTC tests first send
RUN obd2_dtc_seed to the companion: it clears its store and stores P0133, which
also freezes the frame. Each DTC test seeds for itself, so the tests run in any
order and can be repeated.

Every verdict names the bitrate and addressing the firmware was built for, and
these tests assert it against --can-bitrate / --isotp-addr, like test_isotp.py.
"""
import pytest

from test_isotp import _verdict


def _run(agent, can_build, test_id, timeout=8):
    agent.flush()
    agent.cmd(f"RUN {test_id}")
    return _verdict(agent, test_id, can_build, timeout)


def _seed(companion, can_build):
    companion.flush()
    companion.cmd("RUN obd2_dtc_seed")
    line = _verdict(companion, "obd2_dtc_seed", can_build, timeout=5)
    assert b"seeded P0133" in line, line


@pytest.mark.two_board
@pytest.mark.needs_xcvr
def test_obd2_pid_physical(agent, companion, can_build):
    # 01 0C to 0x7E0 -> 41 0C 1A F8 (1726 rpm) from 0x7E8.
    line = _run(agent, can_build, "obd2_pid_phys")
    assert b"rpm physical ok" in line, line


@pytest.mark.two_board
@pytest.mark.needs_xcvr
def test_obd2_pid_functional(agent, companion, can_build):
    # The same kind of question as a broadcast: the answer must still come from
    # the ECU's own response ID, which is the only channel the DUT reads it on.
    line = _run(agent, can_build, "obd2_pid_func")
    assert b"speed broadcast ok" in line, line


@pytest.mark.two_board
@pytest.mark.needs_xcvr
def test_obd2_unsupported(agent, companion, can_build):
    # Physically: 7F 01 12. As a broadcast: nothing at all for a full N_Bs window.
    # The physical half runs first, so the silence cannot be a dead ECU.
    line = _run(agent, can_build, "obd2_unsupported", timeout=12)
    assert b"silent on broadcast" in line, line


@pytest.mark.two_board
@pytest.mark.needs_xcvr
def test_obd2_vin(agent, companion, can_build):
    # 20 bytes: a multi-frame ISO-TP answer from the server.
    line = _run(agent, can_build, "obd2_vin")
    assert b"vin ok" in line, line


@pytest.mark.two_board
@pytest.mark.needs_xcvr
def test_obd2_dtc_cycle(agent, companion, can_build):
    _seed(companion, can_build)
    line = _run(agent, can_build, "obd2_dtc_cycle")
    assert b"P0133 read, cleared, gone" in line, line


@pytest.mark.two_board
@pytest.mark.needs_xcvr
def test_obd2_freeze_frame(agent, companion, can_build):
    _seed(companion, can_build)
    line = _run(agent, can_build, "obd2_freeze")
    assert b"frozen by P0133" in line, line
