# 0003. Catch2 v3 and trompeloeil for tests

Status: Accepted · Date: 2026-10-18

## Context

The project is developed test-first. Unit tests must run without network, Docker or real waiting, which requires replacing I/O collaborators (Modbus client, MQTT client, database writer, clock). Catch2 has no mocking support.

## Decision

- **Catch2 v3** for all C++ tests, one test executable per library, discovered by CTest (`catch_discover_tests`). Labels: `unit`, `local` (needs a local process such as an in-process simulator).
- Integration tests against the Compose stack are the one exception: they are written in pytest (ADR-0012).
- **trompeloeil** for mocks where the interaction itself is the assertion (e.g. "publishes once, retained").
- **Hand-written fakes** for stateful collaborators (a Modbus device with a register map and failure injection, a recording MQTT client, a writer that can be made to fail). Fakes live in test-support targets next to the interface they implement.
- Time is injected (`core::Clock`, `ManualClock`) or passed as an explicit `now`; tests never sleep.

## Consequences

- Logic is pushed into pure, injectable classes (functional core, I/O shell), which is the design we want anyway.
- Two test-double styles to know; the rule of thumb above keeps the choice simple.
- Shells around third-party libraries are covered by integration tests rather than unit tests.
