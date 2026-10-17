# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

# floor2cloud

Reference IIoT platform: shop floor → C++ gateway → MQTT → Kafka → cloud. Open-source portfolio project; everything is simulated (no hardware required).

Full plan (scope, milestones, stack, Milestone 1 tasks and acceptance criteria): `docs/project-plan.md` — read it on demand. Per-milestone specifications: `docs/specs/`. Decisions: `docs/adr/`.

**Status:** pre-implementation. The repository holds only the plan; Milestone 1 (Modbus simulator → gateway → Mosquitto → historian → TimescaleDB → Grafana) is next. Workflow: write/review the milestone spec in `docs/specs/` first, then implement one task at a time, tests first.

## Commands

These are the intended commands once the skeleton exists (CMake presets + vcpkg manifest mode):

- Configure: `cmake --preset dev`
- Build: `cmake --build --preset dev`
- Test: `ctest --preset dev`
- Single test: `ctest --preset dev -R <regex>`, or run the Catch2 test binary directly with a test name / `[tag]` filter
- Run everything: `docker compose -f deploy/compose/docker-compose.yml up`

## Conventions

- C++20, CMake with presets, dependencies via vcpkg (manifest mode, `vcpkg.json`).
- Tests with Catch2 v3; mocks with trompeloeil. TDD: test before implementation.
- Root namespace `f2c` (`f2c::core`, `f2c::drivers`, `f2c::config`, ...).
- Everything in English: code, comments, commits, README, ADRs, specs.
- Plant configuration in YAML: a single file describes the whole plant.
- Formatting: clang-format; static analysis: clang-tidy. Warnings are errors in CI (`-Werror`, GCC and Clang).
- Logging with spdlog. No exceptions crossing thread boundaries.
- Every new architectural decision becomes an ADR in `docs/adr/` (numbered, e.g. `0001-monorepo.md`; format: context, decision, consequences).
- Conventional Commits; one task per pull request.
- Each milestone must stay runnable with one `docker compose up`, no manual steps.

## Architecture

Data flows through four layers: **shop floor** (simulated PLCs, machines, IoT sensors, AMRs) → **edge** (C++ gateway + MQTT broker) → **core** (Kafka and independent consumers) → **storage and visualization** (time-series historian, last-value cache, dashboards).

- **Monorepo, thin services.** `libs/` holds the logic, where it is testable; `services/` are thin executables that only wire library pieces together from configuration. `simulators/` stand in for all hardware; `deploy/` holds everything needed to run the stack.
- **Gateway with pluggable drivers.** Each shop-floor protocol is a driver behind a common interface, chosen from the plant configuration. Drivers only know their protocol and produce normalized tag samples; publishing is a separate concern.
- **Unified Namespace.** All data is addressed by an ISA-95 hierarchy (site/area/line/cell/equipment/tag) and converges on a single MQTT broker at the edge.
- **MQTT at the edge, Kafka at the core.** The broker serves devices and unreliable networks; Kafka (from Milestone 4) adds retention, replay and decoupled consumers. Until then, consumers subscribe to the broker directly.
- **Evolving contracts.** Message formats and topic contracts change across milestones (JSON first, then Sparkplug B, then versioned Protobuf). Treat the current contract as defined by the active milestone's spec and ADRs, not by this file.

Interfaces, data types, wire formats and schemas are defined in `docs/specs/` and `docs/adr/` as each milestone is refined — consult those rather than assuming details from the plan.
