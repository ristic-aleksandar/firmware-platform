"""HIL OTA query self-test (single board, non-destructive).

    pytest --port COM5 test_ota.py

Reports the running slot, next update slot, app version, image size, and rollback
state on-target. A real image swap reboots the board, so it is exercised by the
25_ota demo, not by this in-session harness.
"""


def test_ota_query(agent):
    agent.request("RUN ota_query", rb"@RSP RUN ota_query PASS")
