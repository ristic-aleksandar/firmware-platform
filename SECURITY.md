# Security policy

## Status of this project

This is a learning and portfolio project. It is **not production-ready** and has
not been used in a vehicle or in any product. It was tested only with its own test
suites on a small bench. Do not use it in a safety- or security-relevant system.

The project has had a security audit (14 findings, verdict blocking for product
use). Most fixes were made and re-tested on the bench, and the rest are tracked as
open items. Before you report something, please check whether it is already
known:

- [`docs/security/audit-2026-10-07-full.md`](docs/security/audit-2026-10-07-full.md): the audit report and its re-tests.
- [`docs/SECURITY_HARDENING.md`](docs/SECURITY_HARDENING.md): the open items, and what a product would still have to do (Secure Boot, flash encryption, per-device secrets, BLE pairing, and so on).

## Supported versions

There are no releases. Only the latest commit on `main` is looked at. Nothing is
backported.

## Reporting a vulnerability

Please report privately, not in a public issue:

1. Open the **Security** tab of this repository.
2. Click **Report a vulnerability** (GitHub private vulnerability reporting).

Please include:

- the component or example affected, and the commit you tested;
- what you did, what you expected and what happened;
- how serious you think it is, and why.

Please do not post secrets, keys or other people's data in a report.

## What to expect

This is a one-person project, so replies are best effort. I aim to acknowledge a
report within about 14 days. There is no bug bounty and no guaranteed fix time.
If a report is valid, I will fix it or add it to the tracked open items, and I'm
happy to credit you if you want that.

## Scope

In scope: the firmware components in `components/`, the examples, and the test
and host tooling in `tests/`.

Out of scope:

- ESP-IDF, NimBLE, mbedTLS and other third-party code. Report those upstream.
- Attacks that need physical access to a board, such as flash dumping or fault
  injection. The project documents these as out of scope for the bench setup.
- Issues that only exist because a documented product-hardening step was not
  applied. These are listed in `docs/SECURITY_HARDENING.md`.
- The bench CA certificate and the test tooling that creates throwaway keys
  (their private keys are not in the repository). They are for the bench only
  and must never be reused or trusted elsewhere.
