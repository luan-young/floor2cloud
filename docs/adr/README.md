# Architecture Decision Records

One decision per file, numbered, never renumbered. Format: context, decision, consequences.
A superseded ADR stays in place with its status changed to `Superseded by NNNN`.

Statuses: **Proposed** (drafted, awaiting review) · **Accepted** · **Superseded** · **Deprecated**.

| # | Title | Status |
| --- | --- | --- |
| [0001](0001-monorepo.md) | Monorepo | Accepted |
| [0002](0002-cpp20-cmake-vcpkg.md) | C++20, CMake with presets and vcpkg | Accepted |
| [0003](0003-catch2-trompeloeil.md) | Catch2 v3 and trompeloeil for tests | Accepted |
| [0004](0004-yaml-plant-configuration.md) | Plant configuration in YAML | Accepted |
| [0005](0005-mit-license.md) | MIT license | Accepted |
| [0006](0006-mqtt-client-library.md) | MQTT client library: Eclipse Paho C++ | Accepted |
| [0007](0007-driver-plugin-model.md) | Driver plug-in model: common interface and registry | Accepted |
| [0008](0008-provisional-json-mqtt-contract.md) | Provisional JSON MQTT contract | Accepted |
| [0009](0009-timescaledb-historian.md) | TimescaleDB as the historian | Accepted |
| [0010](0010-gateway-concurrency-model.md) | Gateway concurrency model | Accepted |
| [0011](0011-error-handling.md) | Error handling with `expected` | Accepted |
| [0012](0012-integration-tests-pytest.md) | Integration tests with pytest | Accepted |

## Template

```markdown
# NNNN. Title

Status: Proposed · Date: YYYY-MM-DD

## Context
## Decision
## Consequences
```
