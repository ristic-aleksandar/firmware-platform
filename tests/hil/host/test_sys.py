"""HIL foundation tests (single board): event loop, NVS, watchdog.

    pytest --port COM5 test_sys.py
"""


def test_sys_event(agent):
    agent.request("RUN sys_event", rb"@RSP RUN sys_event PASS", timeout=8)


def test_sys_nvs(agent):
    agent.request("RUN sys_nvs", rb"@RSP RUN sys_nvs PASS")


def test_sys_wdt(agent):
    agent.request("RUN sys_wdt", rb"@RSP RUN sys_wdt PASS")
