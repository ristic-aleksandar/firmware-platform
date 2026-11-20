"""HIL MQTT broker round-trip (automated) — needs Wi-Fi + Mosquitto on this PC.

    $env:HIL_WIFI_SSID = "MyNet" ; $env:HIL_WIFI_PASS = "secret"
    pytest --port COM5 test_net_mqtt.py

The SSID may also be given as --wifi-ssid; the password is read ONLY from the
environment (HIL_WIFI_PASS), never from the command line. The broker listens on
this PC's LAN IP only (not 0.0.0.0); paho here connects to that same address.

This is the automated version of the manual Mosquitto bench test. pytest starts a
local Mosquitto broker, tells the agent (over UART) to join Wi-Fi and connect to
it, then verifies BOTH directions of the pub/sub:
  device -> broker -> PC   (agent publishes hil/mqtt/echo; paho here receives it)
  PC -> broker -> device   (paho publishes hil/mqtt/cmd; the agent reports it back)

Firewall: allow inbound TCP 1883 (the same rule you used for the manual test).
"""
import os
import shutil
import subprocess
import tempfile
import time

import pytest

mqtt = pytest.importorskip("paho.mqtt.client",
                           reason="pip install paho-mqtt to run the MQTT network test")


def _make_client():
    """paho 2.x needs an explicit callback API version; 1.x doesn't have it. We
    only use on_message (its signature is the same across versions), so prefer
    VERSION2 to avoid the v1 deprecation warning."""
    try:
        return mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
    except (AttributeError, TypeError):
        return mqtt.Client()


def _find_mosquitto(config):
    p = config.getoption("--mosquitto")
    if p:
        return p
    found = shutil.which("mosquitto")
    if found:
        return found
    for c in (r"C:\Program Files\mosquitto\mosquitto.exe",
              r"C:\Program Files (x86)\mosquitto\mosquitto.exe"):
        if os.path.exists(c):
            return c
    return None


@pytest.fixture(scope="module")
def broker(request, host_ip):
    exe = _find_mosquitto(request.config)
    if not exe:
        pytest.skip("mosquitto not found (install it or pass --mosquitto <path>)")
    conf = tempfile.NamedTemporaryFile("w", suffix=".conf", delete=False)
    conf.write(f"listener 1883 {host_ip}\nallow_anonymous true\n")
    conf.close()
    proc = subprocess.Popen([exe, "-c", conf.name],
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    time.sleep(1.0)   # let it bind
    if proc.poll() is not None:
        # It exited already — almost always "Address already in use": a Mosquitto
        # service or a leftover broker is holding port 1883.
        out = proc.stdout.read() if proc.stdout else ""
        try:
            os.unlink(conf.name)
        except OSError:
            pass
        pytest.fail(
            "the test's Mosquitto could not start (port 1883 is already in use).\n"
            "Free it, then rerun:\n"
            "  Get-NetTCPConnection -LocalPort 1883        # see who holds it\n"
            "  Get-Service mosquitto | Stop-Service        # stop the service, if any\n"
            "  # or close any 'mosquitto -c ...' window you left running\n"
            f"mosquitto said:\n{out}")
    yield
    proc.terminate()
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()
    try:
        os.unlink(conf.name)
    except OSError:
        pass


@pytest.mark.needs_net
def test_net_mqtt(agent, wifi_creds, host_ip, broker):
    ssid, password = wifi_creds

    # 1) Agent joins Wi-Fi.
    agent.request(f"WIFI {ssid} {password}", rb"@RSP WIFI OK ip=", timeout=25)

    # 2) This PC subscribes (as an MQTT client) to the agent's echo topic.
    received = {}
    cli = _make_client()
    cli.on_message = lambda c, u, m: received.setdefault(m.topic, m.payload)
    cli.connect(host_ip, 1883, 60)   # our own broker; it listens on host_ip only
    cli.subscribe("hil/mqtt/echo")
    cli.loop_start()
    try:
        # 3) Tell the agent to connect + subscribe + publish (its @RSP comes later).
        agent.flush()
        agent.cmd(f"MQTT {host_ip}")

        # 4) device -> broker -> PC: we must receive the agent's echo publish.
        deadline = time.time() + 15
        while received.get("hil/mqtt/echo") is None and time.time() < deadline:
            time.sleep(0.1)
        if received.get("hil/mqtt/echo") != b"device-hello":
            try:  # capture the agent's own view for a useful message
                says = agent.expect(rb"@RSP MQTT [^\r\n]*", timeout=12).group(0).decode("latin1")
            except AssertionError:
                says = "(no @RSP MQTT line from agent)"
            pytest.fail(
                f"no echo via the broker; agent reported: {says}\n"
                f"The ESP32 must be able to reach the broker at {host_ip}:1883 — check "
                f"the broker is bound to {host_ip} (a LAN address, not loopback), inbound "
                "TCP 1883 is allowed through the firewall, and the ESP32 is on the same LAN.")

        # 5) PC -> broker -> device: send the command the agent is waiting for.
        cli.publish("hil/mqtt/cmd", "pc-hello", qos=1)

        # 6) Agent confirms it received our message.
        agent.expect(rb"@RSP MQTT OK rx=pc-hello", timeout=12)
    finally:
        cli.loop_stop()
        cli.disconnect()
