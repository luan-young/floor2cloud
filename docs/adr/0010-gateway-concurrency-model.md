# 0010. Gateway concurrency model

Status: Accepted · Date: 2026-10-18

## Context

The gateway runs several drivers and one publisher. The protocol libraries planned are mostly blocking or bring their own event loop: libmodbus (blocking, per-call timeout), snap7 (blocking), Beckhoff ADS (own threads), open62541 client (own iterate loop). The project rules forbid exceptions crossing thread boundaries and require testability without real time. Options:

1. One shared `io_context` with a thread pool for everything.
2. One `io_context` and one thread per driver, plus a main `io_context` for the publisher.

## Decision

**Option 2.** Each driver gets its own `io_context` running on its own thread; the gateway passes its executor to the driver factory. Blocking protocol calls run on that thread, bounded by the protocol timeout. The `SampleSink` copies samples and posts them to the main `io_context`, where the `SamplePublisher` and its `ChangeFilter` live single-threaded, together with signal handling. Third-party callback threads (Paho) only post to our executors.

**Asio flavour:** standalone Asio (vcpkg `asio`), not Boost.Asio, to avoid pulling Boost into Milestone 1. Revisited if Boost.MQTT5 or Boost.Beast is adopted.

## Consequences

- A slow or hung device cannot delay another device or the publisher; a blocking library is safe by construction.
- No locks in driver or publisher logic: each object is confined to one thread. The only cross-thread operations are `submit()` (a post) and `Driver::stop()`.
- One thread per device does not scale to thousands of devices; it is fine for tens. If needed, cheap non-blocking drivers can later share a pool behind the same factory signature without changing the interface.
- Scheduling logic sits in pure classes (`Poller`, `ChangeFilter`) driven by an injected clock, so threads and timers stay out of unit tests.
- Backoff has no jitter: with one gateway there is no thundering herd. Jitter is added when many gateways share a broker.
