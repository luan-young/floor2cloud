# floor2cloud — Project overview and plan

Oct 14, 2026 · @Luan

## How to use this document with Claude Code

This document is the project's source of context: export it as Markdown and commit it before the first Claude Code session.

1. Create the empty `floor2cloud` repository and save this document as `docs/project-plan.md`.
2. Add a short `CLAUDE.md` at the root (template below). It is read in every session; this plan is read on demand.
3. Open Claude Code in plan mode and ask for the milestone 1 specification first, not the code.
4. Review and adjust the specification, then ask for the implementation one task at a time, tests first.

## Overview and goals

floor2cloud is an open-source reference IIoT platform that takes data from a heterogeneous shop floor to the cloud: it collects, normalizes, distributes, stores, raises alarms and visualizes.

The project has two goals. The first is to be a public portfolio, showing in open code the experience in modern C++, real-time systems, distributed architecture and industrial automation that today lives in proprietary code. The second is to be the vehicle for learning technologies from the study list: MQTT, Kafka, cloud services, Kubernetes, SCADA, Jenkins, Redis, TIA Portal and Step 7, PLC testing, RTOS and AI agents.

Principles that apply to every milestone:

- **One command to run:** anyone who clones the repository brings everything up with `docker compose up`, with no manual steps.
- **Everything simulated:** no milestone requires hardware; real hardware is only an optional extension.
- **Every milestone ends in a demo** that can be recorded as a GIF for the README.
- **Decisions recorded** as ADRs in `docs/adr/`.
- **TDD** with Catch2 from the first commit, and a green CI as a rule.
- **Everything in English:** code, comments, commits, README, ADRs and specifications.

## Simulated scenario

The fictional plant combines three cells from different vendors, standalone IoT sensors and a fleet of AMRs, all simulated.

| Element | Technology | Access protocol | How to simulate | Milestone |
| --- | --- | --- | --- | --- |
| Cell C (legacy machine) | Generic controller | Modbus TCP | In-house C++ simulator with libmodbus | 1 |
| Cell A | Beckhoff PLC | ADS | TwinCAT in trial mode | 2 |
| Cell B | Siemens PLC | S7 and OPC UA | PLCSIM (TIA Portal) or a simulated OPC UA server | 2 |
| IoT sensors | Vibration, temperature, energy | MQTT (Sparkplug B) | In-house simulator; later a real ESP32 | 2 |
| AMR fleet | 3 to 5 robots on ROS 2 | VDA 5050 over MQTT | Lightweight simulator or Nav2 with Gazebo | 3 |

In the namespace, the plant follows the ISA-95 hierarchy: `site/area/line/cell/equipment/tag`. Example: `plant1/production/line1/cell-c/filler/temperature`.

## Architecture and technical decisions

All sources converge on an MQTT broker at the edge, and Kafka distributes the data to independent consumers at the core.

- **Unified Namespace.** ISA-95 hierarchy in the topics. Sparkplug B for PLCs and IoT, VDA 5050 for the fleet. Both run over MQTT, so a single broker serves everything.
- **C++20 gateway with pluggable drivers.** Each protocol is a driver behind a common interface, created from the configuration through a driver registry. Asynchronous I/O with Asio. The whole plant is described in one YAML file.
- **MQTT at the edge, Kafka at the core.** The broker serves devices and unreliable networks; Kafka provides retention, replay and decoupled consumers. In milestone 1 the historian subscribes to the broker directly; Kafka arrives in milestone 4.
- **Storage.** TimescaleDB for history (PostgreSQL with hypertables) and Redis for the last value of each tag.
- **Message format.** JSON in milestone 1 for easy debugging; Sparkplug B (Protobuf) in milestone 2; Protobuf with versioned schemas on Kafka from milestone 4.
- **Alarms as state machines.** ISA-18.2 lifecycle in Boost::SML, one instance per alarm.
- **Deployment.** Docker Compose throughout; Kubernetes with Helm only in milestone 7.

&#91;embedded content: floor2cloud architecture · 4 layers\]

In milestone 1 the historian subscribes to the broker directly; the bridge and Kafka arrive in milestone 4, between the broker and the consumers.

## Technology stack

The base is C++20 with CMake and vcpkg; the choices below are the starting point, and any of them can be revisited in an ADR.

| Area | Choice | Alternative to evaluate | Milestone |
| --- | --- | --- | --- |
| Language and build | C++20, CMake with presets | C++23 where the compiler allows | 1 |
| Dependencies | vcpkg (manifest mode) — decided | — | 1 |
| Plant configuration | YAML with yaml-cpp — decided | rapidyaml | 1 |
| License | MIT — decided | — | 1 |
| Tests | Catch2 v3 | — | 1 |
| Mocks | trompeloeil | FakeIt, or hand-written fakes | 1 |
| Asynchronous I/O | Asio (standalone or Boost) | — | 1 |
| Modbus | libmodbus | In-house client on Asio | 1 |
| MQTT client | Eclipse Paho MQTT C++ | Boost.MQTT5 (async\_mqtt5) | 1 |
| MQTT broker | Mosquitto | EMQX, HiveMQ CE | 1 |
| JSON (MQTT payload) | nlohmann/json | glaze (faster) | 1 |
| Logging | spdlog | — | 1 |
| Database writes | libpqxx | — | 1 |
| Historian | TimescaleDB | InfluxDB | 1 |
| Initial visualization | Grafana | — | 1 |
| CI | GitHub Actions | Jenkins (milestone 7) | 1 |
| OPC UA | open62541 | — | 2 |
| ADS (Beckhoff) | Beckhoff/ADS (official open-source library) | — | 2 |
| S7 (Siemens) | snap7 | — | 2 |
| Sparkplug B | Protobuf + in-house implementation | Eclipse Tahu | 2 |
| Robotics | ROS 2, Nav2 | — | 3 |
| Streaming | Kafka (librdkafka) | Redpanda as the broker | 4 |
| Cache | Redis (redis-plus-plus) | — | 4 |
| State machines | Boost::SML | std::variant + std::visit | 5 |
| API | gRPC + WebSocket (Boost.Beast) | Drogon | 6 |
| User interface | Qt 6/QML | React | 6 |
| Orchestration | Kubernetes + Helm | k3s locally | 7 |

Catch2 has no mocking support, so trompeloeil fills that role.

## Milestones

There are eight milestones, each ending in a demo; milestones 1 to 5 already make a complete portfolio. At 8 to 10 hours per week, milestone 1 takes 3 to 4 weeks and each following one 3 to 6 weeks (rough estimate).

1. **MVP: one data point end to end.**
   - In: Modbus simulator, gateway with one driver, Mosquitto, historian writing to TimescaleDB, Grafana, CI with tests.
   - Out: Kafka, alarms, Sparkplug, security, custom UI.
   - Demo: `docker compose up` and a live chart in Grafana.
2. **Multi-protocol and Unified Namespace.**
   - In: OPC UA, ADS and S7 drivers; Sparkplug B with birth and death messages; IoT sensors publishing straight to the broker.
   - Demo: three cells from different vendors in the same topic tree.
3. **AMR fleet.**
   - In: AMR simulator (or Nav2 with Gazebo), ROS 2 ↔ VDA 5050 bridge, fleet orders and states.
   - Demo: a map with robots moving and their battery levels.
4. **Kafka and consumers.**
   - In: MQTT → Kafka bridge in C++, Protobuf messages with versioned schemas, historian and Redis cache as independent consumers.
   - Demo: take a consumer down and watch it catch up.
5. **Alarms.**
   - In: engine implementing the ISA-18.2 lifecycle in Boost::SML, rules in a file, deadband and delay, notification by e-mail or Telegram.
   - Demo: a fault injected into a cell raising an alarm, acknowledgment and history.
6. **Custom visualization.**
   - In: gRPC and WebSocket API; Qt/QML SCADA or web dashboard with plant overview, fleet map, alarms and trends.
   - Demo: the whole plant on one screen.
7. **Robustness and cloud.**
   - In: store-and-forward in SQLite on the gateway, mTLS, Kubernetes with Helm, managed cloud database, Jenkins pipeline.
   - Demo: cut the gateway's network and lose no data.
8. **Optional extensions.**
   - Real ESP32 with Zephyr, an MCP server so an AI agent can query the plant, a service rewritten in Rust, PLC tests with TcUnit.

## Repository layout and conventions

A single monorepo: a reviewer clones it, runs one command and sees everything working.

```text
floor2cloud/
├── README.md              # diagram, demo GIF, how to run
├── LICENSE                # MIT
├── CLAUDE.md              # context for Claude Code
├── docs/
│   ├── project-plan.md    # this document
│   ├── specs/             # one specification per milestone
│   └── adr/               # architecture decisions, one per file
├── config/                # example plant configurations (YAML)
├── proto/                 # shared Protobuf schemas (milestone 2+)
├── libs/
│   ├── core/              # domain types: tag, value, quality, timestamp
│   ├── config/            # loading and validating the plant configuration
│   ├── drivers/           # modbus/, opcua/, ads/, s7/
│   └── messaging/         # MQTT and Kafka wrappers, serialization
├── services/
│   ├── gateway/
│   ├── historian-writer/
│   ├── mqtt-kafka-bridge/ # milestone 4
│   ├── alarm-engine/      # milestone 5
│   ├── fleet-adapter/     # milestone 3: ROS 2 ↔ VDA 5050
│   └── api/               # milestone 6: gRPC + WebSocket
├── simulators/            # cells, IoT sensors, AMRs
├── ui/                    # milestone 6: scada-qt/ or web/
├── deploy/
│   ├── compose/
│   ├── grafana/           # provisioned datasource and dashboards
│   ├── db/                # TimescaleDB schema and migrations
│   └── helm/              # milestone 7
├── tests/integration/
├── cmake/
├── CMakeLists.txt
├── CMakePresets.json
└── vcpkg.json
```

Conventions:

- Each library in `libs/` is its own CMake target with a public `include/f2c/<lib>/`, `src/` and `tests/`.
- Each service is a thin executable: it reads the configuration, wires up the library pieces and handles signals. The logic lives in the libraries, where it is testable.
- Unit tests next to the code; integration tests in `tests/integration/`, running against the Compose stack.
- Short ADR format: context, decision, consequences. Numbered (`0001-monorepo.md`).
- Conventional Commits; one task per pull request, even when working alone.

## Milestone 1 in detail: end-to-end MVP

Milestone 1 delivers a single working path: a simulated Modbus machine, read by the gateway, published to Mosquitto, written to TimescaleDB and shown in Grafana. Its value is less in the functionality and more in the foundation: build, tests, CI, data model and driver interface that every later milestone reuses.

### Scope

- **Simulator** `simulators/modbus-machine`: a fictional filling machine in C++ with a libmodbus server. Registers for production counter, speed, temperature (with noise), machine state and a fault bit that can be forced.
- **Library** `libs/core`: domain types.
- **Library** `libs/config`: loading and validating the plant YAML file.
- **Library** `libs/drivers/modbus`: Modbus TCP driver with periodic scanning.
- **Library** `libs/messaging`: MQTT publishing and JSON serialization.
- **Service** `services/gateway`: wires configuration, drivers and publishing together.
- **Service** `services/historian-writer`: subscribes to the broker and writes in batches to TimescaleDB.
- **Deployment:** Compose with simulator, gateway, Mosquitto, historian, TimescaleDB and a provisioned Grafana.
- **CI:** GitHub Actions with build, unit tests, clang-tidy and an integration test.

Out of scope: Kafka, Sparkplug B, other protocols, alarms, TLS and authentication, custom UI.

### Data model

Every value flowing through the system is a tag sample with a value, a quality and two timestamps.

```cpp
namespace f2c::core {

using Value = std::variant<bool, std::int64_t, double, std::string>;

enum class Quality { good, uncertain, bad };

using Timestamp = std::chrono::sys_time<std::chrono::nanoseconds>; // always UTC

struct TagSample {
    TagPath   path;          // plant1/production/line1/cell-c/filler/temperature
    Value     value;
    Quality   quality;
    Timestamp source_time;   // when the source produced it (or the read time, if the source has none)
    Timestamp gateway_time;  // when the gateway processed it
};

} // namespace f2c::core
```

`TagPath` is a strong type validated on construction (non-empty segments, allowed characters only).

### Driver interface

A driver knows only its protocol: it reads, converts to `TagSample` and hands the samples to a sink. It knows nothing about MQTT.

```cpp
namespace f2c::drivers {

using SampleSink = std::function<void(std::span<const core::TagSample>)>;

enum class DriverState { stopped, connecting, running, faulted };

class Driver {
public:
    virtual ~Driver() = default;
    virtual std::string_view id() const = 0;
    virtual void start(SampleSink sink) = 0;  // non-blocking; runs on the executor given at construction
    virtual void stop() = 0;                  // idempotent
    virtual DriverState state() const = 0;
};

// Factory: builds a driver from the "driver" block of a device in the configuration.
using DriverFactory = std::function<std::unique_ptr<Driver>(
    asio::any_io_executor, const config::DeviceConfig&)>;

void register_driver(std::string type, DriverFactory factory);

} // namespace f2c::drivers
```

Modbus driver requirements:

- Periodic scanning per tag group, with a configurable period.
- Merge contiguous registers into a single read.
- Types: bool (coil, discrete input), int16, uint16, int32, uint32, float32, with configurable byte and word order.
- Optional linear scaling (`raw * scale + offset`).
- Communication failure: tags go to `bad` quality, with reconnection using bounded exponential backoff.
- Scanning and reconnection logic covered by unit tests that run without a network and without waiting on real time.

### Plant configuration

A single YAML file describes the plant; the gateway refuses to start, with a clear error, if the file is invalid.

```yaml
site: plant1

mqtt:
  uri: tcp://mosquitto:1883
  client_id: gateway-01
  qos: 1

devices:
  - id: filler
    path: production/line1/cell-c/filler
    driver:
      type: modbus-tcp
      host: modbus-machine
      port: 502
      unit_id: 1
    scan_ms: 1000
    tags:
      - { name: counter,     area: holding, address: 0, type: uint32 }
      - { name: speed,       area: holding, address: 2, type: uint16 }
      - { name: temperature, area: input,   address: 0, type: int16, scale: 0.1 }
      - { name: in_fault,    area: coil,    address: 0, type: bool }
```

### MQTT contract

- Topic: `f2c/v1/<site>/<device path>/<tag>`, for example `f2c/v1/plant1/production/line1/cell-c/filler/temperature`.
- JSON payload: `{"v": 41.7, "q": "good", "ts": "2026-10-20T13:05:01.120Z", "gts": "2026-10-20T13:05:01.123Z"}`.
- QoS 1. Publish only when the value or quality changes, with a configurable periodic republish (heartbeat).
- Gateway status at `f2c/v1/<site>/_gateway/<client_id>/status`, retained, with an `offline` last will.
- This contract is provisional: milestone 2 replaces it with Sparkplug B, and the ADR must say so.

### Database

```sql
CREATE TABLE tag_samples (
    time         TIMESTAMPTZ      NOT NULL,
    tag_path     TEXT             NOT NULL,
    value_num    DOUBLE PRECISION,
    value_text   TEXT,
    quality      SMALLINT         NOT NULL,
    gateway_time TIMESTAMPTZ      NOT NULL
);
SELECT create_hypertable('tag_samples', 'time');
CREATE INDEX ON tag_samples (tag_path, time DESC);
```

The historian writes in batches (by size or by time, whichever comes first) and survives the database being unavailable without crashing; losing data in that case is acceptable in milestone 1 and solved in milestone 7.

### Tasks

- [ ] Repository skeleton: MIT `LICENSE`, CMake with presets, `vcpkg.json`, clang-format, clang-tidy, Catch2 integrated with CTest.
- [ ] CI on GitHub Actions: Linux build (GCC and Clang), tests, clang-tidy.
- [ ] Initial ADRs (list below).
- [ ] `libs/core`: `TagPath`, `Value`, `Quality`, `TagSample`, with tests.
- [ ] `libs/config`: YAML loading with yaml-cpp, validation with useful error messages, with tests.
- [ ] `simulators/modbus-machine`: libmodbus server with the filling machine, plus a Dockerfile.
- [ ] `libs/drivers/modbus`: type decoding, read merging, scanning, quality and reconnection, with tests.
- [ ] `libs/messaging`: MQTT publisher, topic building, JSON serialization, publish on change and heartbeat.
- [ ] `services/gateway`: startup, spdlog logging, clean shutdown on signals, status with last will.
- [ ] `deploy/db`: TimescaleDB schema.
- [ ] `services/historian-writer`: subscription, deserialization, batched writes with libpqxx.
- [ ] `deploy/compose`: all services with healthchecks.
- [ ] `deploy/grafana`: provisioned datasource and one dashboard.
- [ ] Integration test: bring up the Compose stack and check that data reaches the database.
- [ ] README with diagram, instructions and the demo GIF.

### Acceptance criteria

- From a clean clone, `docker compose up` brings everything up with no manual steps, and the Grafana dashboard shows live data.
- With a 1 s scan, a value changed in the simulator reaches the database in under 2 s.
- Stopping the simulator takes its tags to `bad` quality within two scans; restarting it restores reading on its own.
- Restarting Mosquitto does not bring down the gateway or the historian; both reconnect.
- The gateway status becomes `offline` when the gateway is killed.
- Green CI: warning-free build (`-Werror`), unit tests, clang-tidy and the integration test.

### Initial ADRs

1. Monorepo.
2. C++20, CMake with presets and vcpkg.
3. Catch2 v3 and trompeloeil for tests.
4. Plant configuration in YAML.
5. MIT license.
6. MQTT client library.
7. Driver plug-in model: common interface and registry.
8. Provisional JSON MQTT contract, to be replaced by Sparkplug B.
9. TimescaleDB as the historian.

## Open questions

Already decided: dependencies via vcpkg, plant configuration in YAML and the MIT license. These remain for the milestone 1 specification and should become ADRs:

- [ ] MQTT client: Paho C++ (mature, widely used) or Boost.MQTT5 (integrates with Asio and coroutines)?
- [ ] Gateway concurrency model: one `io_context` per driver or a shared pool?
