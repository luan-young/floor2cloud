# 0011. Error handling with `expected`

Status: Accepted · Date: 2026-10-18

## Context

Failures here are routine — a device is unplugged, the broker restarts, a config file has a typo — and code runs across several threads where an escaped exception terminates the process. The project rule is that no exception crosses a thread boundary. C++20 has no `std::expected` (C++23).

## Decision

- Fallible library functions return `f2c::core::Expected<T>`, an alias of `tl::expected<T, core::Error>`; `Error` is `{Errc code; std::string message}`. When the project moves to C++23 the alias switches to `std::expected` with no call-site changes.
- The config loader returns `tl::expected<PlantConfig, std::vector<ConfigError>>` to report every error at once.
- Exceptions thrown by third-party libraries (yaml-cpp, libpqxx, Paho, nlohmann/json) are caught in the adapter that calls them and converted to `Error`. No public `f2c` API throws for expected failures.
- Programming errors (violated preconditions) are not `Expected` errors: they are `assert`s in debug builds.
- Handlers posted to an `io_context` and thread entry points are `noexcept` in practice: a top-level catch logs and terminates with exit code 2 if something truly unexpected escapes.

## Consequences

- Failure paths are visible in signatures and testable without exception machinery.
- `[[nodiscard]]` on returning functions makes ignored errors a compiler warning (an error under `-Werror`).
- Slightly more verbose call sites than exceptions; acceptable for the clarity.
- Constructors that can fail are replaced by static factory functions (e.g. `TagPath::parse`).
