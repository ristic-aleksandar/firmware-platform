"""HIL Modbus tests.

Single board (no wire):
    pytest --port COM5 test_modbus.py

Two board (DUT master <-> COMPANION slave over a crossed UART2 link — no CAN
transceiver needed; wire per tests/hil/WIRING.md §8.3). Provision roles first
(SETROLE DUT on A, SETROLE COMPANION on B), then:
    pytest --port COM5 --companion-port COM6 test_modbus.py
"""
import pytest


def test_modbus_selftest(agent):
    # CRC vector + a full master<->slave register round-trip via the pure handler.
    agent.request("RUN modbus_selftest", rb"@RSP RUN modbus_selftest PASS")


@pytest.mark.two_board
def test_modbus_rtu_xfer(agent, companion):
    # The DUT drives the real e32p_modbus UART master against the companion's
    # slave: read 4 holding regs, write + read back one, and confirm an
    # out-of-range read returns a Modbus exception — all over the wire. The
    # returned register values (2000/3000/4000) can't appear without the peer,
    # so the DUT verdict is itself the corroboration.
    companion.flush()
    agent.flush()
    agent.request("RUN modbus_rtu_xfer", rb"@RSP RUN modbus_rtu_xfer PASS", timeout=8)
