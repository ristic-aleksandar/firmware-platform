"""HIL host harness — talks to the agent over raw pyserial.

We use a direct pyserial connection instead of pytest-embedded's serial service:
this DevKit goes unresponsive when RTS is asserted with DTR low (the state
pytest-embedded left the port in). We open with dtr=0/rts=0 — the working state
measured by serial_probe.py — so the agent runs. pytest still drives test
discovery, parametrization, and reporting.

``--port`` is provided by the (installed) pytest-embedded plugin; we just read it.
"""
import os
import re
import socket
import time

import pytest
import serial


def host_lan_ip():
    """Best-effort LAN IP of this PC, so the agent knows where to reach the
    broker / file server we run here. No packet is actually sent."""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("8.8.8.8", 80))
        return s.getsockname()[0]
    except OSError:
        return "127.0.0.1"
    finally:
        s.close()


def pytest_addoption(parser):
    # --port is provided by pytest-embedded; we only add the companion port.
    parser.addoption("--companion-port", action="store", default=None,
                     help="serial port of the read-only COMPANION board (two-board tests)")
    parser.addoption("--with-sensors", action="store_true", default=True,
                     help="run device-driver tests that need a BMP280/TM1637 physically wired")
    parser.addoption("--wifi-ssid", action="store", default=None,
                     help="Wi-Fi SSID for the network tests (MQTT/OTA); their gate. Falls back to "
                          "env HIL_WIFI_SSID; omit both to skip them. The password is read ONLY "
                          "from env HIL_WIFI_PASS (never a command-line option)")
    parser.addoption("--mosquitto", action="store", default=None,
                     help="path to the mosquitto broker exe (default: search PATH + Program Files)")
    parser.addoption("--http-port", action="store", type=int, default=8080,
                     help="port for the OTA test's HTTP file server (allow it through the firewall)")
    parser.addoption("--https-port", action="store", type=int, default=8443,
                     help="port for the https OTA test's file server (allow it through the firewall)")
    parser.addoption("--can-bitrate", action="store", type=int, default=250000,
                     help="CAN bitrate the flashed agent was built for (250000 / 500000 / 1000000); "
                          "ISO-TP tests assert the agent reports it")
    parser.addoption("--isotp-addr", action="store", default="norm11", choices=["norm11", "normfixed"],
                     help="ISO-TP addressing the flashed agent was built for; ISO-TP tests assert it")
    parser.addoption("--canable", action="store", default=None,
                     help="CANable channel for the CAN interop tests, e.g. COM8 (slcan firmware). "
                          "Close Cangaroo first; omit to skip them")


def pytest_configure(config):
    # pytest-embedded stays loaded (only for its --port option) and calls the
    # experimental record_xml_attribute on every test, which warns. We don't use
    # its DUT, so silence that one cosmetic warning by message (robust regardless
    # of how pytest.ini is picked up).
    config.addinivalue_line(
        "filterwarnings", "ignore:record_xml_attribute is an experimental feature")
    # Register markers here too (pytest.ini isn't always picked up as the config
    # file in this layout, but conftest.py always is).
    config.addinivalue_line("markers", "two_board: needs two boards (DUT + COMPANION)")
    config.addinivalue_line("markers", "needs_xcvr: needs a CAN transceiver + termination")
    config.addinivalue_line("markers", "needs_slave: needs slave-mode companion drivers")
    config.addinivalue_line("markers", "needs_sensor: needs a BMP280 and/or TM1637 wired")
    config.addinivalue_line("markers", "needs_net: needs Wi-Fi + this PC as broker/file server")
    config.addinivalue_line("markers", "needs_canable: needs a CANable on the bus; skipped without --canable")


class Agent:
    """A thin serial client for one HIL board running the agent firmware."""

    def __init__(self, port, baud=115200, timeout=0.2):
        self.ser = serial.Serial(port, baud, timeout=timeout)
        self.ser.dtr = False        # dtr=0/rts=0 = the state the board runs in
        self.ser.rts = False
        time.sleep(0.5)             # let it settle / finish booting
        self.ser.reset_input_buffer()
        self._buf = b""

    def close(self):
        try:
            self.ser.close()
        except Exception:
            pass

    def flush(self):
        """Drop any buffered/in-flight input (e.g. a companion's `@EVT` stream).

        The newline comes first: the board's console task accumulates bytes until
        a line ending, so an unterminated fragment left in its buffer would
        prefix the next command and be rejected with `@ERR expected @CMD prefix`.
        Sending the terminator before clearing means the fragment is consumed and
        whatever it produces is dropped along with everything else. A newline on
        an empty buffer costs nothing - the reader ignores empty lines.
        """
        self.ser.write(b"\r\n")
        time.sleep(0.02)
        self.ser.reset_input_buffer()
        self._buf = b""

    def write(self, data):
        if isinstance(data, str):
            data = data.encode()
        self.ser.write(data)

    def cmd(self, verb):
        """Send ``@CMD <verb>``."""
        self.write(f"@CMD {verb}\r\n")

    def expect(self, pattern, timeout=5.0):
        """Read until ``pattern`` (str / bytes / compiled regex) matches; return
        the match. Raises AssertionError on timeout with the recent buffer."""
        if isinstance(pattern, str):
            pattern = pattern.encode()
        pat = pattern if hasattr(pattern, "search") else re.compile(pattern)
        deadline = time.time() + timeout
        while time.time() < deadline:
            chunk = self.ser.read(256)
            if chunk:
                self._buf += chunk
                m = pat.search(self._buf)
                if m:
                    self._buf = self._buf[m.end():]
                    return m
            else:
                time.sleep(0.02)
        raise AssertionError(
            f"timeout waiting for {pattern!r}; last bytes: {self._buf[-200:]!r}")

    def request(self, verb, pattern, timeout=5.0):
        """Send ``@CMD <verb>`` and wait for a matching response line."""
        self.flush()
        self.cmd(verb)
        return self.expect(pattern, timeout)


@pytest.fixture(scope="session")
def agent(request):
    """One serial connection to the DUT for the whole session."""
    port = request.config.getoption("--port")
    a = Agent(port)
    yield a
    a.close()


@pytest.fixture(scope="session")
def sensors(request):
    """Gate for device-driver tests that need a physical part (BMP280/TM1637).
    Tests requesting this fixture auto-skip unless --with-sensors is given."""
    if not request.config.getoption("--with-sensors"):
        pytest.skip("device-driver test — pass --with-sensors (BMP280/TM1637 wired)")


class WifiCreds:
    """(ssid, password) for the network tests. Unpacks like a tuple, but repr()
    hides the password: pytest shows local values in a failure traceback only with
    -l / --showlocals. Do not run the network tests with that option."""

    def __init__(self, ssid, password):
        self.ssid = ssid
        self.password = password

    def __iter__(self):
        return iter((self.ssid, self.password))

    def __repr__(self):
        return "WifiCreds(ssid=<set>, password=<hidden>)"


@pytest.fixture(scope="session")
def wifi_creds(request):
    """Gate for the network tests (MQTT/OTA): they join Wi-Fi and use this PC as
    the broker / file server. The SSID comes from --wifi-ssid or env HIL_WIFI_SSID;
    the password only from env HIL_WIFI_PASS (empty/unset = open network). Skips
    when no SSID is set. The password is never logged or put on a command line."""
    ssid = request.config.getoption("--wifi-ssid") or os.environ.get("HIL_WIFI_SSID")
    if not ssid:
        pytest.skip("network test — set env HIL_WIFI_SSID (or pass --wifi-ssid) and "
                    "env HIL_WIFI_PASS to enable")
    return WifiCreds(ssid, os.environ.get("HIL_WIFI_PASS", ""))


@pytest.fixture(scope="session")
def host_ip():
    """This PC's LAN IP — the address the agent uses for the broker / file server.
    The test servers bind to this address only (not 0.0.0.0)."""
    return host_lan_ip()


@pytest.fixture(scope="session")
def can_build(request):
    """(bitrate, addressing) the flashed agent is expected to be built for. The
    ISO-TP tests assert the agent's own report matches, so a run against a binary
    built for another rate fails loudly instead of silently proving nothing."""
    return request.config.getoption("--can-bitrate"), request.config.getoption("--isotp-addr")


@pytest.fixture(scope="session")
def canable(request):
    """The CANable's channel (slcan). Tests requesting this skip unless --canable is
    given. The tests open and close the adapter themselves, so it is only on the bus
    while one of them runs."""
    channel = request.config.getoption("--canable")
    if not channel:
        pytest.skip("CANable interop test - pass --canable COMx (and close Cangaroo)")
    return channel


@pytest.fixture(scope="session")
def companion(request):
    """Read-only connection to the COMPANION board. Tests that request this
    fixture auto-skip unless --companion-port is given, so single-board runs
    (just --port) skip the two-board tests."""
    port = request.config.getoption("--companion-port")
    if not port:
        pytest.skip("two-board test — pass --companion-port to enable")
    a = Agent(port)
    yield a
    a.close()
