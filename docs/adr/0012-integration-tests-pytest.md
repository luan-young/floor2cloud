# 0012. Integration tests with pytest

Status: Accepted · Date: 2026-10-18

## Context

The Milestone 1 acceptance criteria are end-to-end: bring up the Compose stack, write to the simulator over Modbus, stop and restart containers, kill the gateway, then check rows in TimescaleDB and retained messages on the broker. ADR-0003 makes Catch2 the test framework for C++ code. Integration tests, though, are black-box tests of a running system: most of their code drives `docker compose`, polls with deadlines and talks to Modbus, MQTT, PostgreSQL and HTTP. Doing that in C++ means popen wrappers and much boilerplate.

## Decision

Integration tests are a **pytest** suite in `tests/integration/` (Python 3.12), using pinned `pymodbus`, `paho-mqtt`, `psycopg` and `requests`. A session-scoped fixture starts the stack (`docker compose up -d --build --wait`), attaches `docker compose logs` to failing tests and tears it down. Tests reach the stack only through its exposed ports and the `docker compose` CLI, and wait by polling until a deadline, never by sleeping for a fixed time.

Catch2 remains the framework for all unit and C++ tests (ADR-0003). The integration suite is not registered in CTest; it runs as its own CI job.

## Consequences

- Short, readable tests that map one-to-one to the acceptance criteria, with mature protocol clients.
- The integration tests use independent client implementations (pymodbus, paho-mqtt in Python) rather than the same C++ libraries the services use, so they also catch errors in how the services use their own libraries.
- A second toolchain (Python + pip) in the repository and in CI, limited to `tests/integration/`. Dependencies are pinned in `requirements.txt`.
- Python code needs its own formatting and lint rules (ruff), added together with the suite.
