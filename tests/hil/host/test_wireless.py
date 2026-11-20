"""HIL Wi-Fi / ESP-NOW tests.

Arg-validation runs single board. The ESP-NOW transfer needs a second board
(the ESP-NOW peer companion) and is skipped without --companion-port.

    pytest --port COM5 test_wireless.py
    pytest --port COM5 --companion-port COM6 test_wireless.py   # includes espnow_xfer
"""
import pytest


def test_wl_wifi_neg(agent):
    agent.request("RUN wl_wifi_neg", rb"@RSP RUN wl_wifi_neg PASS")


def test_wl_espnow_neg(agent):
    agent.request("RUN wl_espnow_neg", rb"@RSP RUN wl_espnow_neg PASS")


@pytest.mark.two_board
def test_wl_espnow_xfer(agent, companion):
    companion.flush()
    agent.flush()
    agent.cmd("RUN wl_espnow_xfer")
    # Companion independently reports receiving the DUT's broadcast.
    companion.expect(rb"@EVT ESPNOW from=[0-9a-f:]+ len=8", timeout=6)
    agent.expect(rb"@RSP RUN wl_espnow_xfer PASS", timeout=6)
