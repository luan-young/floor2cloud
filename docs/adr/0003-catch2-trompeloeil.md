# 0003. Catch2 v3 and trompeloeil for tests

Status: Accepted · Date: 2026-10-18 · Amended 2026-10-22 (test labels)

## Context

The project is developed test-first. Unit tests must run without network, Docker or real waiting, which requires replacing I/O collaborators (Modbus client, MQTT client, database writer, clock). Catch2 has no mocking support.

## Decision

- **Catch2 v3** for all C++ tests, one unit-test executable per library, discovered by CTest (`catch_discover_tests`). Labels:
  - `unit`: pure code, no network, no Docker, no waiting. Run by every CI job.
  - `component`: an I/O shell against a real third-party library, with everything inside the test process (e.g. the simulator's libmodbus server driven by a libmodbus client over loopback, on an ephemeral port). Deterministic and fast, so CI runs them too, including under ASan. They live in a separate `<lib>_component_tests` executable and may poll with a deadline, never a bare sleep.
  - `local`: needs a service outside the test process (e.g. a local TimescaleDB). Run by hand; not in CI.
- Integration tests against the Compose stack are the one exception: they are written in pytest (ADR-0012).
- **trompeloeil** for mocks where the interaction itself is the assertion (e.g. "publishes once, retained").
- **Hand-written fakes** for stateful collaborators (a Modbus device with a register map and failure injection, a recording MQTT client, a writer that can be made to fail). Fakes live in test-support targets next to the interface they implement.
- Time is injected (`core::Clock`, `ManualClock`) or passed as an explicit `now`; tests never sleep.

## Consequences

- Logic is pushed into pure, injectable classes (functional core, I/O shell), which is the design we want anyway.
- Two test-double styles to know; the rule of thumb above keeps the choice simple.
- Shells around third-party libraries are covered by component tests where everything fits in the test process, and by integration tests otherwise.
