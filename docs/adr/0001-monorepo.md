# 0001. Monorepo

Status: Accepted · Date: 2026-10-18

## Context

floor2cloud is a portfolio project: a reviewer should clone one repository, run one command and see the whole system working. It contains C++ libraries, several services, simulators, deployment files and documentation that evolve together — a change to the MQTT contract touches the gateway, the historian and the dashboards in the same step.

## Decision

Keep everything in a single repository with the layout described in `docs/project-plan.md`: `libs/` (testable logic), `services/` (thin executables that wire libraries from configuration), `simulators/`, `deploy/`, `docs/`, `tests/integration/`. One top-level CMake project builds all C++ code. Each library is its own CMake target with a public `include/f2c/<lib>/`, `src/` and `tests/`.

## Consequences

- Cross-cutting changes (contract, domain types) land atomically in one pull request, with one CI run proving they fit together.
- One build, one dependency manifest, one version of each dependency for all services.
- CI builds everything on every change; acceptable at this size, and path filters can be added later.
- Services cannot be versioned or released independently. Not needed for a reference platform; revisit only if a component is spun off.
