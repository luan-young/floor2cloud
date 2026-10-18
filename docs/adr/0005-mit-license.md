# 0005. MIT license

Status: Accepted · Date: 2026-10-18

## Context

floor2cloud is a public portfolio and learning project. It should be easy for anyone to read, reuse and fork. It depends on third-party components with various licenses.

## Decision

The project is licensed under **MIT** (`LICENSE` at the root). Third-party licenses relevant to Milestone 1:

| Component | License | How it is used |
| --- | --- | --- |
| libmodbus | LGPL-2.1+ | Linked (statically by default, ADR-0002) |
| Eclipse Paho C / C++ | EPL-2.0 or EDL-1.0 (BSD-3) | Linked |
| yaml-cpp, nlohmann/json, spdlog, fmt, Catch2, Asio | MIT / BSL-1.0 | Linked or header-only |
| tl-expected | CC0 | Header-only |
| libpqxx / libpq | BSD-3 / PostgreSQL | Linked |
| trompeloeil | BSL-1.0 | Tests only |
| Mosquitto | EPL-2.0 / EDL-1.0 | Unmodified container |
| TimescaleDB | Apache-2.0 + Timescale License (retention policies) | Unmodified container |
| Grafana OSS | AGPL-3.0 | Unmodified container |

## Consequences

- Static linking of LGPL libmodbus is compliant because the complete source of floor2cloud is public and users can rebuild and relink against a modified libmodbus. If binaries are ever distributed without source, switch to a dynamic triplet for libmodbus.
- Running AGPL/TSL software as unmodified containers imposes no obligation on this repository's code.
- A `THIRD_PARTY_NOTICES` file is added if Docker images are published.
