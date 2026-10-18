# 0004. Plant configuration in YAML

Status: Accepted · Date: 2026-10-18

## Context

The gateway must be configured for a whole plant — broker, devices, protocols, tags — without recompiling. The file is written by hand, reviewed in pull requests and read by people from automation backgrounds. A wrong entry must be caught at startup, not discovered as missing data.

## Decision

- One YAML file describes the whole plant (site, MQTT, devices, tags). Parsed with **yaml-cpp**.
- Loading produces typed structs (`f2c::config::PlantConfig`) with every default and override resolved; nothing downstream reads YAML.
- Validation is strict and complete: unknown keys are errors (with "did you mean" suggestions), all errors are reported in one run with file, line, column and key path, and the gateway refuses to start if any exist.
- Service-level settings that are not part of the plant (e.g. the historian's database URI) are environment variables, not part of this file.

## Consequences

- Comments and anchors make large plant files manageable; the format is familiar to anyone who has used Compose or Kubernetes.
- Strictness means a new optional key requires a code change in the loader — intended, since an unknown key is more often a typo than a feature.
- yaml-cpp throws exceptions; the loader catches them and converts to `ConfigError` (ADR-0011).
- rapidyaml stays an option if load time ever matters; it does not at this scale.
