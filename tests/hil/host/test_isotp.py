"""HIL ISO-TP (ISO 15765-2) tests - slice 1.6 of docs/tickets/diagnostics-stack.md.

Single-board tests need the CAN transceiver (their self-reception loop runs
through it) but no companion:
    pytest test_isotp.py --port COM5

Two-board tests exchange with the COMPANION's ISO-TP peer - echo on 0x7E2/0x7EA,
an echo that pauses the sender with FC.WAIT on 0x7E3/0x7EB, and an abandoned
response on 0x7E4/0x7EC:
    pytest test_isotp.py --port COM5 --companion-port COM7

Every agent verdict names the bitrate and addressing it was built for, and these
tests assert that against --can-bitrate / --isotp-addr (defaults 250000 / norm11).
A binary built for another rate fails loudly instead of silently proving nothing.

During these runs a CANable must be unplugged, or at the same bitrate in
listen-only mode.
"""
import pytest


def _verdict(agent, test_id, can_build, timeout):
    """Wait for the agent's verdict line, require PASS, and require that the
    firmware reports the build this session was told to expect. The pattern includes the
    line ending: serial data arrives in chunks, so without it the match can
    complete on a partial line and cut the detail short."""
    m = agent.expect(rb"@RSP RUN " + test_id.encode() + rb" (PASS|FAIL)[^\r\n]*\r?\n", timeout=timeout)
    line = m.group(0)
    assert b" PASS" in line, line
    rate, addr = can_build
    assert f"rate={rate} addr={addr}".encode() in line, \
        f"agent was built for a different bitrate or addressing: {line!r}"
    return line


def _echo(agent, companion, can_build, test_id, length, channel, timeout):
    companion.flush()
    agent.flush()
    agent.cmd(f"RUN {test_id}")
    # The companion independently reports the payload it reassembled and echoed.
    companion.expect(f"@EVT ISOTP rx ch={channel} len={length} echo=ok".encode(), timeout=timeout)
    line = _verdict(agent, test_id, can_build, timeout)
    assert f"len={length}".encode() in line, line


# ----------------------------------------------------------------------------
# Single board + transceiver (no companion needed).
# ----------------------------------------------------------------------------

@pytest.mark.needs_xcvr
def test_isotp_self(agent, can_build):
    # Engine + link over the real controller: two channels looped through
    # self-reception, 100 bytes each way.
    agent.flush()
    agent.cmd("RUN isotp_self")
    line = _verdict(agent, "isotp_self", can_build, timeout=12)
    assert b"len=100" in line, line


@pytest.mark.needs_xcvr
def test_isotp_lifecycle(agent, can_build):
    # Three start/attach/stop cycles leave no task and no heap behind, and a
    # transfer afterwards is not robbed by a leftover pump (ticket D-03).
    agent.flush()
    agent.cmd("RUN isotp_lifecycle")
    _verdict(agent, "isotp_lifecycle", can_build, timeout=15)


# ----------------------------------------------------------------------------
# Two boards + transceivers.
# ----------------------------------------------------------------------------

@pytest.mark.two_board
@pytest.mark.needs_xcvr
def test_isotp_sf(agent, companion, can_build):
    _echo(agent, companion, can_build, "isotp_sf", 6, "echo", timeout=8)


@pytest.mark.two_board
@pytest.mark.needs_xcvr
def test_isotp_mf(agent, companion, can_build):
    _echo(agent, companion, can_build, "isotp_mf", 100, "echo", timeout=10)


@pytest.mark.two_board
@pytest.mark.needs_xcvr
def test_isotp_big(agent, companion, can_build):
    _echo(agent, companion, can_build, "isotp_big", 1024, "echo", timeout=15)


@pytest.mark.two_board
@pytest.mark.needs_xcvr
def test_isotp_fcwait(agent, companion, can_build):
    _echo(agent, companion, can_build, "isotp_fcwait", 60, "fcwait", timeout=10)


@pytest.mark.two_board
@pytest.mark.needs_xcvr
def test_isotp_ncr(agent, companion, can_build):
    # The companion opens a response with a First Frame and abandons it; the DUT
    # must discard the session on N_Cr and deliver nothing.
    companion.flush()
    agent.flush()
    agent.cmd("RUN isotp_ncr")
    companion.expect(rb"@EVT ISOTP ncr ff-sent", timeout=6)
    line = _verdict(agent, "isotp_ncr", can_build, timeout=8)
    assert b"N_Cr discarded" in line, line
