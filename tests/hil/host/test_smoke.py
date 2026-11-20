"""HIL smoke tests — single board (the DUT), over raw serial.

Proves the agent command loop end-to-end: liveness, SYSINFO, LIST, RUN (with
heap/timing diagnostics), the unknown-test error path, and NVS role persistence.
No wiring needed.

Prereq: flash the agent to the board first (your normal BOOT/RST flash), then:
    pytest --port COM5 test_smoke.py
"""


def test_agent_alive(agent):
    agent.request("PING", rb"@RSP PING OK")


def test_sysinfo(agent):
    agent.request("SYSINFO", rb"@RSP SYSINFO chip=\S+ cores=\d+ heap=\d+")


def test_list_contains_ping(agent):
    agent.request("LIST", rb"@RSP LIST \S*ping")


def test_run_ping_passes(agent):
    # RUN carries heap-delta + timing diagnostics.
    agent.request("RUN ping", rb"@RSP RUN ping PASS heap=[+-]?\d+ t=\d+ms")


def test_run_sysinfo_passes(agent):
    agent.request("RUN sysinfo", rb"@RSP RUN sysinfo PASS")


def test_run_unknown_errors(agent):
    agent.request("RUN nope", rb"@ERR unknown test: nope")


def test_setrole_persists(agent):
    agent.request("SETROLE DUT", rb"@RSP SETROLE OK")
    agent.request("ROLE", rb"@RSP ROLE DUT")
