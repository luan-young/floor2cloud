# 0008. Provisional JSON MQTT contract

Status: Accepted · Date: 2026-10-18

## Context

Milestone 1 needs a message format between the gateway and the historian that is easy to inspect with `mosquitto_sub` while the pipeline is being built. Milestone 2 adopts Sparkplug B for PLCs and IoT; Milestone 4 adds versioned Protobuf on Kafka.

## Decision

Milestone 1 uses a **provisional JSON contract**, fully specified in `docs/specs/m1-mvp.md` § 6:

- One topic per tag: `f2c/v1/<site>/<area>/<line>/<cell>/<equipment>/<tag>`; `v1` is the contract version.
- Payload `{"v": …, "q": "good|uncertain|bad", "ts": "<RFC 3339 ms UTC>", "gts": "…"}`; `v` may be `null` (no value), boolean, integer, double or string; doubles always carry a fraction so types round-trip.
- QoS 1, not retained; publish on change of value or quality, plus a per-tag heartbeat.
- Gateway status at `f2c/v1/<site>/_gateway/<client_id>/status`, retained, with an `offline` last will. Segments starting with `_` are reserved for system topics.
- Serialization with **nlohmann/json** (glaze is faster; not needed at this volume).

**This contract will be replaced by Sparkplug B in Milestone 2.** No consumer outside this repository should rely on it.

## Consequences

- Human-readable messages make the first milestone easy to debug and demo.
- One message per tag with string keys is verbose and has no birth/death semantics or metric aliases; Sparkplug B solves these, which is why it replaces this contract.
- The historian and the dashboards will need changing in Milestone 2; the codec is isolated in `libs/messaging` to keep that change contained.
- Millisecond precision on the wire limits sample resolution to 1 ms, well below the 50 ms minimum scan.
