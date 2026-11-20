"""HIL low-power tests (single board, no wiring, non-disruptive).

    pytest --port COM5 test_pm.py

Deep sleep resets the board (dropping this serial session), so it is exercised by
the 22_pm demo rather than here. Light sleep returns, so it is checked live.
"""


def test_pm_wakecause(agent):
    agent.request("RUN pm_wakecause", rb"@RSP RUN pm_wakecause PASS")


def test_pm_lightsleep(agent):
    # Board light-sleeps ~50 ms then wakes on the timer; give it headroom.
    agent.request("RUN pm_lightsleep", rb"@RSP RUN pm_lightsleep PASS", timeout=5.0)
