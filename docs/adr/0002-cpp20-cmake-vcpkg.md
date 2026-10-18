# 0002. C++20, CMake with presets and vcpkg

Status: Accepted · Date: 2026-10-18

## Context

The gateway, historian and simulators are C++. The project must build reproducibly on a developer machine, in CI and inside Docker, on GCC and Clang, with dependencies that range from header-only (Asio, nlohmann/json) to C libraries (libmodbus, Paho C, libpq).

## Decision

- **Language:** C++20. C++23 features only behind a decision (e.g. replacing `tl::expected` with `std::expected`, ADR-0011).
- **Compilers:** GCC 13 and Clang 18 (Ubuntu 24.04 defaults), both in CI.
- **Build:** CMake ≥ 3.25 with `CMakePresets.json` (`dev`, `release`, `asan`, `ci-gcc`, `ci-clang`). Developers and CI use the same presets.
- **Dependencies:** vcpkg in manifest mode (`vcpkg.json`) with a pinned `builtin-baseline`; toolchain found through `VCPKG_ROOT`. Default static triplet (see ADR-0005 for the license implication).
- **Quality gates:** `-Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror` through a shared interface target; clang-format and clang-tidy enforced in CI.

## Consequences

- One manifest pins every dependency version for every environment; Docker and CI reuse it.
- First builds are slow (vcpkg compiles from source); mitigated by vcpkg binary caching in CI and BuildKit cache mounts in Docker.
- `-Werror` on two compilers will occasionally fail on a new compiler warning; the fix is to address the warning, not to relax the flag.
- Presets make the commands in `CLAUDE.md` the same everywhere.
