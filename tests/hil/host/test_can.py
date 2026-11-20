"""HIL CAN / J1939 tests (Phase 1).

Single-board tests run on the DUT alone (no wiring) — flash the agent, then:
    pytest --port COM5 test_can.py

Two-board tests need a COMPANION board + CAN transceivers and are skipped unless
you pass --companion-port. Provision roles first (SETROLE DUT on A, SETROLE
COMPANION on B), wire per tests/hil/WIRING.md, then:
    pytest --port COM5 --companion-port COM6 test_can.py
"""
import pytest


# ----------------------------------------------------------------------------
# Single board — verified now (TWAI NO_ACK / in-process loopback / pure codec).
# ----------------------------------------------------------------------------

def test_can_twai_neg(agent):
    agent.request("RUN can_twai_neg", rb"@RSP RUN can_twai_neg PASS")


def test_can_twai_selftx(agent):
    agent.request("RUN can_twai_selftx", rb"@RSP RUN can_twai_selftx PASS")


def test_can_j1939_codec(agent):
    agent.request("RUN can_j1939_codec", rb"@RSP RUN can_j1939_codec PASS")


def test_can_j1939_neg(agent):
    agent.request("RUN can_j1939_neg", rb"@RSP RUN can_j1939_neg PASS")


def test_can_tp_loop(agent):
    agent.request("RUN can_tp_loop", rb"@RSP RUN can_tp_loop PASS", timeout=10)


def test_can_tp_neg(agent):
    agent.request("RUN can_tp_neg", rb"@RSP RUN can_tp_neg PASS")


def test_can_acl_contend(agent):
    # Address claiming: two engines contend in-process, lower NAME wins.
    agent.request("RUN can_acl_contend", rb"@RSP RUN can_acl_contend PASS", timeout=10)


# ----------------------------------------------------------------------------
# Two boards + transceivers — written now, run when the rig exists. Each is
# corroborated by the companion's independent @EVT stream where applicable.
# ----------------------------------------------------------------------------

@pytest.mark.two_board
@pytest.mark.needs_xcvr
def test_can_twai_xfer(agent, companion):
    companion.flush()
    agent.flush()
    agent.cmd("RUN can_twai_xfer")
    # Companion independently reports receiving the DUT's frame (SA 0x80).
    companion.expect(rb"@EVT J1939 pgn=\d+ sa=0x80", timeout=6)
    agent.expect(rb"@RSP RUN can_twai_xfer PASS", timeout=6)


@pytest.mark.two_board
@pytest.mark.needs_xcvr
def test_can_j1939_single(agent, companion):
    companion.flush()
    agent.flush()
    agent.cmd("RUN can_j1939_single")
    companion.expect(rb"@EVT J1939 pgn=65262 sa=0x80", timeout=6)
    agent.expect(rb"@RSP RUN can_j1939_single PASS", timeout=6)


@pytest.mark.two_board
@pytest.mark.needs_xcvr
def test_can_j1939_rx(agent, companion):
    # The DUT receives the companion's autonomous ~1 Hz broadcast (SA 0x21).
    agent.flush()
    agent.cmd("RUN can_j1939_rx")
    agent.expect(rb"@RSP RUN can_j1939_rx PASS .*sa=0x21", timeout=8)


@pytest.mark.two_board
@pytest.mark.needs_xcvr
def test_can_tp_bam(agent, companion):
    companion.flush()
    agent.flush()
    agent.cmd("RUN can_tp_bam")
    # Companion reassembles the 250-byte BAM message from the DUT.
    companion.expect(rb"@EVT TP pgn=\d+ sa=0x80 len=250", timeout=15)
    agent.expect(rb"@RSP RUN can_tp_bam PASS", timeout=15)


@pytest.mark.two_board
@pytest.mark.needs_xcvr
def test_can_tp_cmdt(agent, companion):
    companion.flush()
    agent.flush()
    agent.cmd("RUN can_tp_cmdt")
    # RTS/CTS to the companion (SA 0x21); it reassembles and EndOfMsgAcks.
    companion.expect(rb"@EVT TP pgn=\d+ sa=0x80 len=250", timeout=15)
    agent.expect(rb"@RSP RUN can_tp_cmdt PASS", timeout=15)


@pytest.mark.two_board
@pytest.mark.needs_xcvr
def test_can_stress(agent, companion):
    agent.flush()
    agent.cmd("RUN can_stress")
    agent.expect(rb"@RSP RUN can_stress PASS", timeout=20)
