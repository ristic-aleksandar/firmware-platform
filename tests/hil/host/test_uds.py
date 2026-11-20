"""HIL UDS (ISO 14229-1) tests - slice 3.6 of docs/tickets/diagnostics-stack.md.

The COMPANION runs a UDS server (ECU 0x11: requests on 0x7E1, broadcast on 0x7DF,
answers on 0x7E9). It shares its trouble codes with the companion's OBD-II server,
keeps downloads in RAM, and restarts for real on 11 01. The DUT plays the tester:

    pytest test_uds.py --port COM5 --companion-port COM7

Security access uses HMAC-SHA256 with a fixed HIL TEST SECRET, written in
hil_diag.c - a test fixture, never a product key (item 4 of the security audit
in docs/SECURITY_HARDENING.md).

Every test starts and ends in the default session, so they run in any order. Three
take long on purpose: the S3 test waits out the 5 s session timeout, and the lockout
test and the after-reset test each wait out the 10 s security delay, so later tests
start unlocked.

The companion saves "the last security access failed" in NVS. A run aborted inside any
of the security tests can leave that flag at 1: the companion then refuses a seed for
10 s after its next boot. The next run clears it once at least
10 s have passed since the companion booted: test_uds_security or the opening unlock of
test_uds_security_delay_after_reset sends a right key, which saves 0.

Every verdict names the bitrate and addressing the firmware was built for, and
these tests assert it against --can-bitrate / --isotp-addr, like test_isotp.py.
"""
import pytest

from test_isotp import _verdict


def _run(agent, can_build, test_id, timeout=10):
    agent.flush()
    agent.cmd(f"RUN {test_id}")
    return _verdict(agent, test_id, can_build, timeout)


@pytest.mark.two_board
@pytest.mark.needs_xcvr
def test_uds_session_and_s3(agent, companion, can_build):
    # 10 03, then a silent 3E 80 keeps it alive past 5 s; then 5.3 s of silence
    # drops it back to default, where 27 01 is refused with 0x7F.
    line = _run(agent, can_build, "uds_session", timeout=20)
    assert b"S3 fell back to default" in line, line


@pytest.mark.two_board
@pytest.mark.needs_xcvr
def test_uds_functional(agent, companion, can_build):
    # A broadcast 3E 00 is answered from the ECU's own ID; 3E 80 and an unsupported
    # service are answered physically but never on broadcast.
    line = _run(agent, can_build, "uds_functional")
    assert b"silent on broadcast" in line, line


@pytest.mark.two_board
@pytest.mark.needs_xcvr
def test_uds_security(agent, companion, can_build):
    line = _run(agent, can_build, "uds_security")
    assert b"relocked" in line, line


@pytest.mark.two_board
@pytest.mark.needs_xcvr
def test_uds_did_write(agent, companion, can_build):
    line = _run(agent, can_build, "uds_did_write")
    assert b"read back" in line, line


@pytest.mark.two_board
@pytest.mark.needs_xcvr
def test_uds_dtc_shared_with_obd2(agent, companion, can_build):
    # One store, two views: a code set through UDS is read by OBD-II 03, and a
    # UDS 14 clears it for OBD-II too.
    line = _run(agent, can_build, "uds_dtc")
    assert b"seen by OBD 03, cleared by 14" in line, line


@pytest.mark.two_board
@pytest.mark.needs_xcvr
def test_uds_erase_pending(agent, companion, can_build):
    # The 2 s erase: 7F 31 78 within P2, 0x21 for a request meanwhile, then 71 01 FF00 00.
    line = _run(agent, can_build, "uds_erase", timeout=15)
    assert b"0x21 while pending" in line, line


@pytest.mark.two_board
@pytest.mark.needs_xcvr
def test_uds_download(agent, companion, can_build):
    # Erase, 34, four 1026-byte 36 blocks, 37, then FF01 with the right and a wrong CRC.
    line = _run(agent, can_build, "uds_download", timeout=20)
    assert b"matched, a wrong one caught" in line, line


@pytest.mark.two_board
@pytest.mark.needs_xcvr
def test_uds_lockout(agent, companion, can_build):
    # Three wrong keys: 0x36, then 0x37 even after a session change, until 10 s pass.
    line = _run(agent, can_build, "uds_lockout", timeout=25)
    assert b"unlock after" in line, line


@pytest.mark.two_board
@pytest.mark.needs_xcvr
def test_uds_ecu_reset(agent, companion, can_build):
    # 11 01 restarts the companion for real. Its serial port stays open through the
    # restart (the USB bridge is a separate chip), so the host sees it go and come
    # back, and only then asks the DUT to check what a reboot must have erased.
    companion.flush()
    line = _run(agent, can_build, "uds_reset")
    assert b"51 01 after setting" in line, line
    companion.expect(b"@EVT UDS reset: restarting", timeout=3)
    companion.expect(b"@EVT READY role=COMPANION", timeout=10)
    companion.expect(b"@EVT UDS server ready", timeout=10)
    line = _run(agent, can_build, "uds_after_reset")
    assert b"rebooted" in line, line


@pytest.mark.two_board
@pytest.mark.needs_xcvr
def test_uds_security_delay_after_reset(agent, companion, can_build):
    # Three wrong keys, then 11 01. The companion saves "the last access failed" and
    # restarts. Right after it is back, a seed must be refused (0x37); once the delay
    # is over a right key unlocks and clears the saved flag.
    companion.flush()
    line = _run(agent, can_build, "uds_sec_reset")
    assert b"51 01" in line, line
    companion.expect(b"@EVT UDS sec flag=1", timeout=3)
    companion.expect(b"@EVT UDS reset: restarting", timeout=3)
    companion.expect(b"@EVT READY role=COMPANION", timeout=10)
    companion.expect(b"@EVT UDS server ready", timeout=10)
    line = _run(agent, can_build, "uds_sec_after", timeout=25)
    assert b"0x37 after the reset" in line, line
    companion.expect(b"@EVT UDS sec flag=0", timeout=3)
