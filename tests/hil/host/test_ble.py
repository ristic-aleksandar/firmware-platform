"""HIL BLE self-test (single board, no radio).

    pytest --port COM5 test_ble.py

Verifies the pure advertising-payload assembly + UUID helpers on-target. The
connectable peripheral (advertise + a central) is the 27_ble demo, which needs
the Bluetooth host and a phone/central, so it isn't part of this UART harness.
"""


def test_ble_selftest(agent):
    agent.request("RUN ble_selftest", rb"@RSP RUN ble_selftest PASS")
