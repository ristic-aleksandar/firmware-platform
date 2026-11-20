"""HIL OTA round-trips over http and https (automated) — need Wi-Fi; this PC serves the .bin.

    $env:HIL_WIFI_SSID = "MyNet" ; $env:HIL_WIFI_PASS = "secret"
    pytest --port COM5 test_net_ota.py

The SSID may also be given as --wifi-ssid; the password is read ONLY from the
environment (HIL_WIFI_PASS), never from the command line. The file servers bind
to this PC's LAN IP only (not 0.0.0.0).

The automated version of the manual `python -m http.server` OTA test. pytest
serves the agent's own image (tests/hil/agent/build/hil_agent.bin), tells the agent
to pull it, waits for the reboot, and asserts the running OTA slot flipped
(ota_0 <-> ota_1) — proving the full pull -> flash -> reboot cycle.

test_net_ota uses plain http (the agent is built with ALLOW_HTTP for this).
test_net_ota_https uses https: the server cert is made fresh for each run and
signed by the HIL bench CA, which the agent trusts through its CA bundle. It also
checks the main task's stack high-water mark after a full download (>= 1024 bytes).
One-time setup for it: `python -I tools/make_bench_ca.py`, then rebuild and flash
the agent. Without the CA key (tests/hil/host/.bench_ca/) it skips. It needs the
`cryptography` package (requirements.txt). The high-water mark is printed and
recorded as the property main_stack_hwm_bytes; see it with `-rP` or `-s`.

Firewall: allow inbound TCP on --http-port (default 8080; the same rule you used
for the manual test) and --https-port (default 8443). The agent reboots during
these tests and re-emits @EVT READY.
"""
import datetime
import functools
import http.server
import ipaddress
import os
import shutil
import ssl
import tempfile
import threading

import pytest

AGENT_BIN = os.path.normpath(
    os.path.join(os.path.dirname(__file__), "..", "agent", "build", "hil_agent.bin"))
HOST_DIR = os.path.dirname(os.path.abspath(__file__))
# Made once by tools/make_bench_ca.py. The .der is built into the agent's CA bundle.
CA_DER = os.path.normpath(os.path.join(HOST_DIR, "..", "agent", "bench_ca", "hil_bench_ca.der"))
CA_KEY = os.path.join(HOST_DIR, ".bench_ca", "hil_bench_ca.key")
MIN_MAIN_STACK_HWM = 1024   # bytes free on the main task after the pull (audit F114)


@pytest.fixture(scope="module")
def http_server(request, host_ip):
    if not os.path.exists(AGENT_BIN):
        pytest.skip("agent not built (hil_agent.bin not found) — build the HIL agent first")
    root = os.path.dirname(AGENT_BIN)
    port = request.config.getoption("--http-port")
    handler = functools.partial(http.server.SimpleHTTPRequestHandler, directory=root)
    try:
        srv = http.server.ThreadingHTTPServer((host_ip, port), handler)
    except OSError as e:
        pytest.skip(f"cannot bind HTTP {host_ip}:{port} ({e}); free it or pass --http-port")
    t = threading.Thread(target=srv.serve_forever, daemon=True)
    t.start()
    yield port
    srv.shutdown()


@pytest.mark.needs_net
def test_net_ota(agent, wifi_creds, host_ip, http_server):
    ssid, password = wifi_creds
    port = http_server

    # 1) Agent joins Wi-Fi.
    agent.request(f"WIFI {ssid} {password}", rb"@RSP WIFI OK ip=", timeout=25)

    # 2) Record the slot we're running from now.
    before = agent.request("OTASLOT", rb"@RSP OTASLOT (ota_\d)", timeout=5).group(1)

    # 3) Pull the agent's own image from this PC; the agent flashes it and reboots.
    url = f"http://{host_ip}:{port}/{os.path.basename(AGENT_BIN)}"
    agent.flush()
    agent.cmd(f"OTA {url}")
    agent.expect(rb"@RSP OTA STAGED reboot", timeout=90)   # download (~20s) + stage
    agent.expect(rb"@EVT READY", timeout=30)               # boots into the new slot

    # 4) The running slot must have flipped.
    after = agent.request("OTASLOT", rb"@RSP OTASLOT (ota_\d)", timeout=5).group(1)
    assert before != after, f"OTA slot did not change ({before!r} -> {after!r})"


def _make_server_cert(host_ip, out_dir):
    """Write a fresh ECDSA P-256 server cert + key for host_ip, signed by the bench
    CA, into out_dir. Returns (cert_path, key_path)."""
    from cryptography import x509
    from cryptography.hazmat.primitives import hashes, serialization
    from cryptography.hazmat.primitives.asymmetric import ec
    from cryptography.x509.oid import NameOID

    with open(CA_DER, "rb") as f:
        ca_cert = x509.load_der_x509_certificate(f.read())
    with open(CA_KEY, "rb") as f:
        ca_key = serialization.load_pem_private_key(f.read(), password=None)
    spki = (serialization.Encoding.DER, serialization.PublicFormat.SubjectPublicKeyInfo)
    if ca_key.public_key().public_bytes(*spki) != ca_cert.public_key().public_bytes(*spki):
        pytest.fail("the bench CA key does not match agent/bench_ca/hil_bench_ca.der "
                    "(re-run tools/make_bench_ca.py --force, rebuild and flash the agent)")

    now = datetime.datetime.now(datetime.timezone.utc)
    key = ec.generate_private_key(ec.SECP256R1())
    san = [x509.IPAddress(ipaddress.ip_address(host_ip)), x509.DNSName(host_ip)]
    cert = (x509.CertificateBuilder()
            .subject_name(x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, host_ip)]))
            .issuer_name(ca_cert.subject).public_key(key.public_key())
            .serial_number(x509.random_serial_number())
            .not_valid_before(now - datetime.timedelta(hours=1))
            .not_valid_after(now + datetime.timedelta(days=1))
            .add_extension(x509.SubjectAlternativeName(san), critical=False)
            .add_extension(x509.BasicConstraints(ca=False, path_length=None), critical=True)
            .sign(ca_key, hashes.SHA256()))
    del ca_key

    cert_path = os.path.join(out_dir, "server.crt")
    key_path = os.path.join(out_dir, "server.key")
    with open(cert_path, "wb") as f:
        f.write(cert.public_bytes(serialization.Encoding.PEM))
    with open(key_path, "wb") as f:
        f.write(key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
                                  serialization.NoEncryption()))
    return cert_path, key_path


@pytest.fixture(scope="module")
def https_server(request, host_ip):
    if not os.path.exists(AGENT_BIN):
        pytest.skip("agent not built (hil_agent.bin not found) — build the HIL agent first")
    if not (os.path.exists(CA_KEY) and os.path.exists(CA_DER)):
        pytest.skip("no bench CA key — run tools/make_bench_ca.py, rebuild and flash the agent")
    pytest.importorskip("cryptography", reason="the https OTA test needs `cryptography` "
                                               "(pip install -r requirements.txt)")
    root = os.path.dirname(AGENT_BIN)
    port = request.config.getoption("--https-port")
    tmp = tempfile.mkdtemp(prefix="hil_ota_tls_")   # server cert + key, this run only
    try:
        cert_path, key_path = _make_server_cert(host_ip, tmp)
        ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        ctx.minimum_version = ssl.TLSVersion.TLSv1_2
        ctx.load_cert_chain(cert_path, key_path)
        handler = functools.partial(http.server.SimpleHTTPRequestHandler, directory=root)
        try:
            srv = http.server.ThreadingHTTPServer((host_ip, port), handler)
        except OSError as e:
            pytest.skip(f"cannot bind HTTPS {host_ip}:{port} ({e}); free it or pass --https-port")
        srv.socket = ctx.wrap_socket(srv.socket, server_side=True)
        t = threading.Thread(target=srv.serve_forever, daemon=True)
        t.start()
        yield port
        srv.shutdown()
        srv.server_close()
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


@pytest.mark.needs_net
def test_net_ota_https(agent, wifi_creds, host_ip, https_server, record_property):
    ssid, password = wifi_creds
    port = https_server

    agent.request(f"WIFI {ssid} {password}", rb"@RSP WIFI OK ip=", timeout=25)
    before = agent.request("OTASLOT", rb"@RSP OTASLOT (ota_\d)", timeout=5).group(1)

    # Pull over https; the agent checks the server cert against its CA bundle.
    url = f"https://{host_ip}:{port}/{os.path.basename(AGENT_BIN)}"
    agent.flush()
    agent.cmd(f"OTA {url}")
    # Emitted right after the pull returns, before the @RSP line.
    hwm = int(agent.expect(rb"@EVT STACK main hwm=(\d+)", timeout=150).group(1))
    record_property("main_stack_hwm_bytes", hwm)
    print(f"main task stack high-water mark after the https pull: {hwm} bytes")
    agent.expect(rb"@RSP OTA STAGED reboot", timeout=10)
    agent.expect(rb"@EVT READY", timeout=30)

    after = agent.request("OTASLOT", rb"@RSP OTASLOT (ota_\d)", timeout=5).group(1)
    assert before != after, f"OTA slot did not change ({before!r} -> {after!r})"
    assert hwm >= MIN_MAIN_STACK_HWM, (
        f"main task stack high-water mark {hwm} B < {MIN_MAIN_STACK_HWM} B after an https OTA")
