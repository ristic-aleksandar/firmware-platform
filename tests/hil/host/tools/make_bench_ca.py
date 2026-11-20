"""Make the HIL bench CA once (for the https OTA test, test_net_ota_https).

    python -I tools/make_bench_ca.py            # from tests/hil/host
    python -I tools/make_bench_ca.py --force    # replace an existing CA (then rebuild + reflash)

Writes:
  tests/hil/agent/bench_ca/hil_bench_ca.der   public CA cert. The agent build adds it to
                                              its CA bundle (sdkconfig.defaults).
  tests/hil/host/.bench_ca/hil_bench_ca.key   CA private key. Git-ignored (*.key). It
                                              stays on this PC; the test uses it to sign
                                              a fresh server cert for each run.

The CA is EC P-256, valid 10 years, and named as a test-only CA. Trust it only in the
HIL agent. Prints file names, sizes and the cert SHA-256 prefix, never key material.
"""
import argparse
import datetime
import hashlib
import os
import sys

from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.x509.oid import NameOID

HOST_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CA_DER = os.path.normpath(os.path.join(HOST_DIR, "..", "agent", "bench_ca", "hil_bench_ca.der"))
CA_KEY = os.path.join(HOST_DIR, ".bench_ca", "hil_bench_ca.key")
CA_CN = "HIL bench CA - test only, never trust elsewhere"


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--force", action="store_true",
                    help="replace an existing CA key and cert (rebuild and reflash the agent after)")
    args = ap.parse_args()

    if os.path.exists(CA_KEY) and not args.force:
        sys.exit(f"refused: {CA_KEY} exists. The flashed agent trusts its cert. "
                 "Pass --force to replace it, then rebuild and reflash the agent.")

    now = datetime.datetime.now(datetime.timezone.utc)
    key = ec.generate_private_key(ec.SECP256R1())
    name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, CA_CN)])
    cert = (x509.CertificateBuilder().subject_name(name).issuer_name(name)
            .public_key(key.public_key()).serial_number(x509.random_serial_number())
            .not_valid_before(now - datetime.timedelta(days=1))
            .not_valid_after(now + datetime.timedelta(days=3650))
            .add_extension(x509.BasicConstraints(ca=True, path_length=0), critical=True)
            .add_extension(x509.KeyUsage(digital_signature=True, key_cert_sign=True, crl_sign=True,
                                         content_commitment=False, key_encipherment=False,
                                         data_encipherment=False, key_agreement=False,
                                         encipher_only=False, decipher_only=False), critical=True)
            .sign(key, hashes.SHA256()))

    der = cert.public_bytes(serialization.Encoding.DER)
    pem_key = key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
                                serialization.NoEncryption())
    del key

    os.makedirs(os.path.dirname(CA_KEY), exist_ok=True)
    os.makedirs(os.path.dirname(CA_DER), exist_ok=True)
    flags = os.O_WRONLY | os.O_CREAT | (os.O_TRUNC if args.force else os.O_EXCL)
    fd = os.open(CA_KEY, flags | getattr(os, "O_BINARY", 0), 0o600)
    with os.fdopen(fd, "wb") as f:
        f.write(pem_key)
    with open(CA_DER, "wb") as f:
        f.write(der)

    print(f"{CA_DER} {len(der)} bytes, sha256 {hashlib.sha256(der).hexdigest()[:16]}")
    print(f"{CA_KEY} {len(pem_key)} bytes (private key, git-ignored, not printed)")
    print("Next: rebuild and reflash the HIL agent so its CA bundle has this cert.")


if __name__ == "__main__":
    main()
