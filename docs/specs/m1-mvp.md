# Milestone 1 — End-to-end MVP: specification

Status: **Accepted** · Oct 18, 2026 · Source: `docs/project-plan.md` § "Milestone 1 in detail"

This document turns the Milestone 1 plan into something implementable: requirements, public C++ interfaces, configuration format, MQTT contract, database schema, test plan and task order. Where it deviates from the plan, the change is listed in [§ 10](#10-deviations-from-the-project-plan). Decisions that still need your call are in [§ 11](#11-open-decisions).

## Contents

1. [Goal and scope](#1-goal-and-scope)
2. [Requirements](#2-requirements)
3. [Architecture and code layout](#3-architecture-and-code-layout)
4. [C++ interfaces](#4-c-interfaces)
5. [Plant configuration](#5-plant-configuration)
6. [MQTT contract](#6-mqtt-contract)
7. [Database schema](#7-database-schema)
8. [Deployment](#8-deployment)
9. [Test plan](#9-test-plan)
10. [Deviations from the project plan](#10-deviations-from-the-project-plan)
11. [Open decisions](#11-open-decisions)
12. [Task order](#12-task-order)

---

## 1. Goal and scope

One working path from a simulated machine to a chart:

```text
modbus-machine ──Modbus TCP──▶ gateway ──MQTT/JSON──▶ mosquitto ──▶ historian-writer ──SQL──▶ TimescaleDB ──▶ Grafana
 (simulator)                  (C++20)                  (broker)       (C++20)
```

The milestone's lasting value is the foundation that later milestones reuse: the build, CI, domain types, configuration loader, driver interface and the testing approach (functional core, thin I/O shell).

**In scope:** Modbus TCP simulator; libraries `core`, `config`, `drivers` (+ `drivers/modbus`), `messaging`, `storage`; services `gateway` and `historian-writer`; Compose stack with Mosquitto, TimescaleDB and provisioned Grafana; GitHub Actions CI with unit tests, clang-tidy and an integration test.

**Out of scope:** Kafka, Sparkplug B, other protocols, alarms, TLS and authentication, Redis, custom UI, store-and-forward (data loss during outages is accepted; Milestone 7 fixes it), writes from the gateway to devices.

## 2. Requirements

Identifiers are referenced by the test plan.

### 2.1 Simulator (`simulators/modbus-machine`)

| ID | Requirement |
| --- | --- |
| SIM-1 | Serves Modbus TCP (libmodbus) on a configurable port (default 502), unit id 1, accepting several concurrent clients. |
| SIM-2 | Models a filling machine with the register map in [§ 2.1.1](#211-simulator-register-map), advancing the model every 100 ms (configurable). |
| SIM-3 | Temperature has Gaussian noise; the random seed is configurable (`--seed`) so runs can be reproduced. |
| SIM-4 | A client can force a fault by writing coil 0 and change the speed setpoint by writing holding register 3. Writes to read-only registers are overwritten on the next model step. |
| SIM-5 | The model (`FillerModel`) is a pure class with no I/O, covered by unit tests. |
| SIM-6 | Ships as a Docker image; exits cleanly on SIGINT/SIGTERM. |

#### 2.1.1 Simulator register map

All 32-bit values use big-endian word order (high word in the lower address) and big-endian bytes within each register.

| Area | Address | Type | Access | Name | Meaning |
| --- | --- | --- | --- | --- | --- |
| holding | 0–1 | uint32 | R | `counter` | Bottles filled since start; wraps at 2³². |
| holding | 2 | uint16 | R | `speed` | Current speed, bottles/min. Ramps toward the setpoint at 10 bpm/s. |
| holding | 3 | uint16 | R/W | `speed_setpoint` | Target speed, bottles/min, 0–300. Default 120. 0 stops the machine. |
| holding | 4 | uint16 | R | `state` | 0 = stopped, 1 = running, 2 = faulted. |
| input | 0 | int16 | R | `temperature` | Product temperature in 0.1 °C. ~25 °C idle, rising toward ~45 °C at full speed (first-order lag), σ = 0.2 °C noise. |
| coil | 0 | bool | R/W | `in_fault` | Writing `true` forces a fault: speed drops to 0 immediately, state = faulted, counter stops. Writing `false` clears it. |

### 2.2 Gateway (`services/gateway` + libraries)

| ID | Requirement |
| --- | --- |
| GW-1 | Starts with `f2c-gateway --config <file>` (or `F2C_PLANT_CONFIG`). Invalid configuration → exits with code 1 and prints **every** validation error with file, line, column and key path, before starting anything. |
| GW-2 | Builds one driver per device through the driver registry, chosen by `driver.type`. Unknown type is a configuration error. |
| GW-3 | Modbus driver scans tags periodically per scan group (tags of one device with the same period). |
| GW-4 | Contiguous or overlapping addresses in the same area and scan group are merged into one request, up to 125 registers or 2000 bits per request. |
| GW-5 | Decodes bool (coil, discrete input), int16, uint16, int32, uint32, float32, with configurable byte order and word order (device default, per-tag override). |
| GW-6 | Optional linear scaling `raw * scale + offset`; a scaled value is a `double`, an unscaled integer stays `int64`. Non-finite float32 values produce `bad` quality. |
| GW-7 | Connection failure (connect error, I/O error, timeout) → every tag of the device is emitted once with `bad` quality and its last known value (or no value), the connection is closed and reconnection uses bounded exponential backoff (default 500 ms → 10 s, ×2). A Modbus exception response affects only the tags of that request; the connection is kept. |
| GW-8 | Response timeout defaults to 500 ms, so with a 1 s scan a stopped device turns `bad` within two scans. |
| GW-9 | Publishes each sample to MQTT per [§ 6](#6-mqtt-contract): only when value or quality changes, plus a per-tag heartbeat that republishes the latest sample (default every 30 s, 0 disables). |
| GW-10 | Publishes a retained `online` status on every successful (re)connection to the broker, registers a retained `offline` last will, and publishes `offline` explicitly on clean shutdown. |
| GW-11 | Starts and keeps running while the broker is down; reconnects automatically. Messages produced while disconnected are buffered up to 10 000 (oldest dropped). |
| GW-12 | SIGINT/SIGTERM → stops drivers, publishes `offline`, disconnects (2 s timeout), exits 0. |
| GW-13 | Logs with spdlog (level via `--log-level`, default `info`). State transitions (connected, disconnected, recovered) are logged once at `info`/`warn`; individual retries at `debug`. |

### 2.3 Historian writer (`services/historian-writer` + `libs/storage`)

| ID | Requirement |
| --- | --- |
| HIS-1 | Subscribes to `f2c/v1/#` with QoS 1 and a persistent session (`clean_session = false`, fixed client id), so the broker queues messages while it is down. |
| HIS-2 | Ignores system topics (any segment starting with `_`) silently; logs malformed topics or payloads at `warn`, rate-limited to one line per 10 s, and counts them. |
| HIS-3 | Writes in batches: a batch is flushed when it reaches `max_batch` samples (default 500) or the oldest buffered sample is older than `max_age` (default 500 ms), whichever comes first. |
| HIS-4 | Writes are idempotent: duplicate deliveries (QoS 1) do not create duplicate rows. |
| HIS-5 | When the database is unavailable it does not crash: it keeps buffering (bounded, default 100 000 samples, oldest dropped and counted) and retries with bounded exponential backoff. |
| HIS-6 | Configured by environment variables ([§ 8.3](#83-historian-configuration)). |
| HIS-7 | SIGINT/SIGTERM → stops consuming, attempts one final flush (2 s budget), exits 0. |

### 2.4 Non-functional

| ID | Requirement |
| --- | --- |
| NF-1 | From a clean clone, `docker compose -f deploy/compose/docker-compose.yml up` starts everything with no manual steps; Grafana shows live data without logging in. |
| NF-2 | End-to-end latency (value change in the simulator → row in the database) < 2 s with a 1 s scan. |
| NF-3 | Restarting Mosquitto brings down neither the gateway nor the historian; both reconnect. |
| NF-4 | Warning-free build with `-Wall -Wextra -Wpedantic -Werror` (+ `-Wconversion -Wshadow`) on GCC 13 and Clang 18; clang-tidy clean; clang-format clean. |
| NF-5 | Unit tests need no network, no Docker and no real waiting: anything time-dependent takes a `core::Clock` or an explicit `now`. |
| NF-6 | No exception crosses a thread boundary. Fallible library APIs return `core::Expected<T>`; exceptions from third-party libraries are caught at the adapter that calls them. |
| NF-7 | Sized for growth, not for M1: the design must not need changing to handle ~10 devices / 1 000 tags/s, but M1 sets no throughput target beyond the demo. |

## 3. Architecture and code layout

### 3.1 Threads

See [ADR-0010](../adr/0010-gateway-concurrency-model.md).

```text
gateway process
├── main thread: io_context #main  ── signals, SamplePublisher (ChangeFilter, heartbeat timer)
├── driver thread "filler": io_context #filler ── ModbusDriver (timer → Poller → blocking libmodbus calls)
│        └── sink(samples) ── post ──▶ #main
└── Paho threads ── publish, reconnect, callbacks (post to #main)

historian-writer process
├── main thread: io_context ── signals, BatchFlusher timer, blocking libpqxx writes
└── Paho threads ── on_message: decode → post to main
```

Each driver owns an `io_context` and a thread so that a blocking protocol library (libmodbus now; snap7, ADS and open62541 in Milestone 2) can never stall another device or the publisher. All publisher state is confined to the main `io_context`, so it needs no locks.

### 3.2 Functional core, I/O shell

Every component with logic splits into a pure part that is unit-tested exhaustively and a thin Asio/library shell:

| Pure core (unit-tested) | Shell (covered by integration test) |
| --- | --- |
| `drivers::modbus::plan_scan_groups`, `decode_*`, `Poller` | `ModbusDriver` (timer), `LibmodbusClient` |
| `messaging::ChangeFilter`, topic and JSON codec | `SamplePublisher` (strand + timer), `PahoMqttClient` |
| `storage::SampleBatcher`, `BatchFlusher`, `to_row` | `HistorianPipeline` (timer), `PgSampleWriter` |
| `sim::FillerModel` | libmodbus server loop |

### 3.3 Repository layout added by M1

```text
libs/
├── core/              f2c::core        (target f2c::core; test support: f2c::core_testing)
├── config/            f2c::config      (yaml-cpp)
├── drivers/           f2c::drivers     (Driver, DriverRegistry)
│   └── modbus/        f2c::drivers::modbus (libmodbus)   target f2c::drivers_modbus
├── messaging/         f2c::messaging   (Paho, nlohmann/json)
└── storage/           f2c::storage     (libpqxx)          ← new, not in the plan's layout
services/gateway/, services/historian-writer/
simulators/modbus-machine/             f2c::sim
config/plant1.yaml
deploy/{compose,db,grafana,docker}/
tests/integration/
```

Each library: `include/f2c/<lib>/…`, `src/`, `tests/`. One Catch2 executable per library (`f2c_<lib>_tests`), registered with `catch_discover_tests`, CTest label `unit`; component tests, where a library has them, in `f2c_<lib>_component_tests` (label `component`, see § 9.1). Integration tests are a pytest suite in `tests/integration/` (ADR-0012), outside CTest.

Library dependencies (no cycles): `core` ← `config` ← `drivers` ← `drivers_modbus`; `core` ← `messaging`; `core` ← `storage`. `messaging` and `storage` do not depend on `config` beyond plain option structs they define themselves; services translate.

## 4. C++ interfaces

The headers below are the public contract of each library. They are complete enough to write tests against; private members and implementation detail are omitted (`// …`). Includes are trimmed to the non-obvious ones.

### 4.1 `libs/core`

```cpp
// f2c/core/error.hpp
#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <tl/expected.hpp>   // see ADR-0011; becomes std::expected when we move to C++23

namespace f2c::core {

enum class Errc : std::uint8_t {
    invalid_argument,  // malformed input: tag path, payload field, config value
    parse_error,       // syntax error: YAML, JSON, timestamp
    not_found,         // unknown driver type, missing file
    io_error,          // socket or file I/O failure, connection lost
    timeout,           // no response within the configured time
    protocol_error,    // peer answered with an error (e.g. Modbus exception)
    unavailable,       // dependency not reachable (broker, database)
};

[[nodiscard]] std::string_view to_string(Errc code) noexcept;

struct Error {
    Errc code;
    std::string message;  // human-readable, includes context ("filler: read holding 0..4: timeout")
};

template <typename T>
using Expected = tl::expected<T, Error>;

[[nodiscard]] inline tl::unexpected<Error> make_error(Errc code, std::string message) {
    return tl::unexpected<Error>(Error{code, std::move(message)});
}

} // namespace f2c::core
```

```cpp
// f2c/core/tag_path.hpp
#pragma once
#include <span>
#include <string>
#include <vector>
#include "f2c/core/error.hpp"

namespace f2c::core {

inline constexpr std::size_t max_tag_path_depth = 16;
inline constexpr std::size_t max_segment_length = 64;

// ISA-95 address: site/area/line/cell/equipment/tag, slash-separated.
// Invariants (checked on construction):
//   - 1 to 16 segments;
//   - each segment matches [a-z0-9][a-z0-9_-]{0,63}.
// Lowercase only, so "Filler" and "filler" can never be two topics or two series.
// Segments starting with '_' are reserved for system topics (e.g. "_gateway") and rejected.
// The ISA-95 depth is a convention of the configuration, not enforced here.
class TagPath {
public:
    static Expected<TagPath> parse(std::string_view text);

    // Fails if the result would break an invariant.
    [[nodiscard]] Expected<TagPath> append(std::string_view segment) const;
    [[nodiscard]] Expected<TagPath> join(const TagPath& suffix) const;

    [[nodiscard]] const std::string& str() const noexcept;
    [[nodiscard]] std::vector<std::string_view> segments() const;
    [[nodiscard]] std::size_t depth() const noexcept;

    friend bool operator==(const TagPath&, const TagPath&) = default;
    friend auto operator<=>(const TagPath&, const TagPath&) = default;

private:
    explicit TagPath(std::string text);
    std::string text_;
};

[[nodiscard]] bool is_valid_segment(std::string_view segment) noexcept;

} // namespace f2c::core

template <>
struct std::hash<f2c::core::TagPath> {
    std::size_t operator()(const f2c::core::TagPath& p) const noexcept;
};
```

```cpp
// f2c/core/timestamp.hpp
#pragma once
#include <chrono>
#include "f2c/core/error.hpp"

namespace f2c::core {

using Timestamp = std::chrono::sys_time<std::chrono::nanoseconds>;  // always UTC

// "2026-10-20T13:05:01.120Z": exactly three fractional digits, 'Z' suffix, truncated (floor) to ms.
[[nodiscard]] std::string format_rfc3339_ms(Timestamp t);

// Accepts RFC 3339 with 'Z' or a ±hh:mm offset and 0 to 9 fractional digits. The 'T' and 'Z'
// separators may be lowercase, as RFC 3339 allows. Leap seconds (":60") are rejected: Timestamp
// cannot represent them.
[[nodiscard]] Expected<Timestamp> parse_rfc3339(std::string_view text);

} // namespace f2c::core
```

```cpp
// f2c/core/tag_sample.hpp
#pragma once
#include <cstdint>
#include <string>
#include <variant>
#include "f2c/core/tag_path.hpp"
#include "f2c/core/timestamp.hpp"

namespace f2c::core {

// std::monostate = "no value": a bad-quality sample of a tag that has never been read.
using Value = std::variant<std::monostate, bool, std::int64_t, double, std::string>;

enum class Quality : std::uint8_t { good, uncertain, bad };  // M1 drivers emit only good and bad

[[nodiscard]] std::string_view to_string(Quality q) noexcept;           // "good" | "uncertain" | "bad"
[[nodiscard]] Expected<Quality> parse_quality(std::string_view text);

struct TagSample {
    TagPath   path;
    Value     value;
    Quality   quality;
    Timestamp source_time;   // when the source produced it; the read-completion time if the source has none
    Timestamp gateway_time;  // when the gateway processed it (set by SamplePublisher; drivers copy source_time)

    friend bool operator==(const TagSample&, const TagSample&) = default;
};

} // namespace f2c::core
```

```cpp
// f2c/core/clock.hpp
#pragma once
#include <chrono>
#include "f2c/core/timestamp.hpp"

namespace f2c::core {

// Injected wherever code needs "now", so tests can control time (NF-5).
// Not copyable or movable: always passed by reference.
class Clock {
public:
    Clock() = default;
    Clock(const Clock&) = delete;
    Clock& operator=(const Clock&) = delete;
    Clock(Clock&&) = delete;
    Clock& operator=(Clock&&) = delete;
    virtual ~Clock() = default;
    [[nodiscard]] virtual Timestamp now() const = 0;                                // wall clock, for timestamps
    [[nodiscard]] virtual std::chrono::steady_clock::time_point steady_now() const = 0;  // for scheduling
};

// The process-wide real clock: std::chrono::system_clock and std::chrono::steady_clock.
[[nodiscard]] const Clock& system_clock() noexcept;

} // namespace f2c::core

// f2c/core/testing/manual_clock.hpp   (target f2c::core_testing, linked only by tests;
// lives in libs/core/testing/ so production code cannot include it)
namespace f2c::core::testing {

// Only moves when told to. Not thread-safe: tests advance it from the thread that reads it.
class ManualClock final : public Clock {
public:
    explicit ManualClock(Timestamp start = Timestamp{});
    Timestamp now() const override;
    std::chrono::steady_clock::time_point steady_now() const override;
    void advance(std::chrono::nanoseconds d);  // moves both clocks; d >= 0 (asserted)
    // …
};

} // namespace f2c::core::testing
```

```cpp
// f2c/core/backoff.hpp
#pragma once
#include <chrono>

namespace f2c::core {

struct BackoffPolicy {
    std::chrono::milliseconds initial{500};
    std::chrono::milliseconds max{10'000};
    double multiplier{2.0};  // >= 1.0
};

// Deterministic bounded exponential backoff (no jitter in M1: a single gateway, see ADR-0010).
class Backoff {
public:
    // Preconditions (asserted): 0 <= initial <= max, multiplier >= 1.0.
    explicit Backoff(BackoffPolicy policy);
    [[nodiscard]] std::chrono::milliseconds next_delay();  // initial, initial*m, …, capped at max
    void reset() noexcept;
    // …
};

} // namespace f2c::core
```

### 4.2 `libs/config`

```cpp
// f2c/config/plant_config.hpp
#pragma once
#include <chrono>
#include <optional>
#include <variant>
#include <vector>
#include "f2c/core/backoff.hpp"
#include "f2c/core/tag_path.hpp"

namespace f2c::config {

struct MqttConfig {
    std::string uri;                         // tcp://host:port
    std::string client_id;                   // must be a valid TagPath segment (used in the status topic)
    int qos{1};                              // 0..2
    std::chrono::seconds keepalive{10};
    std::chrono::seconds heartbeat{30};      // 0 disables
};

enum class ModbusArea : std::uint8_t { coil, discrete_input, holding, input };
enum class ModbusType : std::uint8_t { boolean, int16, uint16, int32, uint32, float32 };
enum class ByteOrder : std::uint8_t { big, little };  // bytes inside each 16-bit register
enum class WordOrder : std::uint8_t { big, little };  // register order of 32-bit values (big = high word first)

struct ModbusTcpConfig {
    std::string host;
    std::uint16_t port{502};
    std::uint8_t unit_id{1};
    std::chrono::milliseconds timeout{500};
    core::BackoffPolicy reconnect{};
    ByteOrder byte_order{ByteOrder::big};    // device defaults, overridable per tag
    WordOrder word_order{WordOrder::big};
};

struct ModbusAddress {
    ModbusArea area;
    std::uint16_t address;
    ModbusType type;
    ByteOrder byte_order;                    // resolved: tag override or device default
    WordOrder word_order;
};

// Closed sets: Milestone 2 adds OPC UA, ADS and S7 alternatives (ADR-0007).
using DriverConfig = std::variant<ModbusTcpConfig>;
using TagAddress   = std::variant<ModbusAddress>;

struct LinearScaling {
    double scale{1.0};
    double offset{0.0};
};

struct TagConfig {
    std::string name;                        // last path segment
    core::TagPath path;                      // full: site/device path/name
    std::chrono::milliseconds scan_period;   // resolved: tag override or device default
    std::optional<LinearScaling> scaling;
    TagAddress address;
};

struct DeviceConfig {
    std::string id;                          // unique in the file; used for logs and Driver::id()
    core::TagPath path;                      // full: site/device path
    DriverConfig driver;
    std::vector<TagConfig> tags;
};

struct PlantConfig {
    std::string site;
    MqttConfig mqtt;
    std::vector<DeviceConfig> devices;
};

[[nodiscard]] std::string_view driver_type(const DriverConfig& d) noexcept;  // "modbus-tcp"

} // namespace f2c::config
```

```cpp
// f2c/config/loader.hpp
#pragma once
#include <filesystem>
#include <span>
#include <tl/expected.hpp>
#include "f2c/config/plant_config.hpp"

namespace f2c::config {

struct ConfigError {
    std::string location;   // "config/plant1.yaml:14:9"; the file alone if not tied to a node
    std::string key_path;   // "devices[0].tags[2].type"; empty for the whole document
    std::string message;    // "unknown type 'uint64'; expected one of: bool, int16, uint16, int32, uint32, float32"
};

// A valid configuration, plus findings that do not prevent loading (e.g. timeout >= scan period).
struct LoadedConfig {
    PlantConfig plant;
    std::vector<ConfigError> warnings;
};

// All errors are collected; loading never stops at the first one. Never throws.
using LoadResult = tl::expected<LoadedConfig, std::vector<ConfigError>>;

[[nodiscard]] LoadResult load_plant_config(const std::filesystem::path& file);
[[nodiscard]] LoadResult parse_plant_config(std::string_view yaml, std::string_view source_name = "<string>");

// One "<location>: <key path>: <message>" line per error; empty parts are left out.
[[nodiscard]] std::string format_errors(std::span<const ConfigError> errors);

} // namespace f2c::config
```

### 4.3 `libs/drivers`

```cpp
// f2c/drivers/driver.hpp
#pragma once
#include <functional>
#include <span>
#include "f2c/core/tag_sample.hpp"

namespace f2c::drivers {

// Called on the driver's thread. Must be thread-safe and must not block (it should copy and post).
using SampleSink = std::function<void(std::span<const core::TagSample>)>;

enum class DriverState { stopped, connecting, running, faulted };

class Driver {
public:
    virtual ~Driver() = default;
    [[nodiscard]] virtual std::string_view id() const = 0;

    // Non-blocking. Work runs on the executor given at construction. Call at most once.
    virtual void start(SampleSink sink) = 0;

    // Idempotent. Blocks until in-flight I/O finishes (bounded by the protocol timeout);
    // after it returns the sink is never called again.
    // Precondition: not called from the driver's own executor.
    virtual void stop() = 0;

    [[nodiscard]] virtual DriverState state() const = 0;  // thread-safe
};

} // namespace f2c::drivers
```

```cpp
// f2c/drivers/registry.hpp
#pragma once
#include <asio/any_io_executor.hpp>
#include <map>
#include <memory>
#include "f2c/config/plant_config.hpp"
#include "f2c/drivers/driver.hpp"

namespace f2c::drivers {

using DriverFactory = std::function<core::Expected<std::unique_ptr<Driver>>(
    asio::any_io_executor, const config::DeviceConfig&)>;

// An instance, not global state: services register what they link explicitly (ADR-0007).
class DriverRegistry {
public:
    // Error (invalid_argument) if the type is already registered.
    core::Expected<void> add(std::string type, DriverFactory factory);

    // Error (not_found) if no factory matches config::driver_type(device.driver).
    [[nodiscard]] core::Expected<std::unique_ptr<Driver>> create(
        asio::any_io_executor executor, const config::DeviceConfig& device) const;

    [[nodiscard]] bool contains(std::string_view type) const;

private:
    std::map<std::string, DriverFactory, std::less<>> factories_;
};

} // namespace f2c::drivers
```

### 4.4 `libs/drivers/modbus`

```cpp
// f2c/drivers/modbus/modbus_client.hpp
#pragma once
#include <vector>
#include "f2c/config/plant_config.hpp"

namespace f2c::drivers::modbus {

// Blocking Modbus client. Not thread-safe: owned and used by one driver thread.
// Error codes: io_error / timeout = connection is unusable (caller closes and reconnects);
//              protocol_error = Modbus exception response (connection still fine).
class ModbusClient {
public:
    virtual ~ModbusClient() = default;
    virtual core::Expected<void> connect() = 0;
    virtual void close() noexcept = 0;
    [[nodiscard]] virtual bool connected() const noexcept = 0;

    // area: coil or discrete_input; count 1..2000
    virtual core::Expected<std::vector<bool>> read_bits(
        config::ModbusArea area, std::uint16_t address, std::uint16_t count) = 0;

    // area: holding or input; count 1..125; registers in host order as received
    virtual core::Expected<std::vector<std::uint16_t>> read_registers(
        config::ModbusArea area, std::uint16_t address, std::uint16_t count) = 0;
};

// libmodbus-backed implementation (sets the response timeout from the config).
[[nodiscard]] std::unique_ptr<ModbusClient> make_libmodbus_client(const config::ModbusTcpConfig& cfg);

} // namespace f2c::drivers::modbus
```

```cpp
// f2c/drivers/modbus/read_plan.hpp
#pragma once
#include <chrono>
#include <span>
#include <vector>
#include "f2c/config/plant_config.hpp"

namespace f2c::drivers::modbus {

inline constexpr std::uint16_t max_registers_per_read = 125;
inline constexpr std::uint16_t max_bits_per_read = 2000;

[[nodiscard]] std::uint16_t width(config::ModbusType type) noexcept;  // registers (1 or 2); 1 for boolean

struct ReadBlock {
    config::ModbusArea area;
    std::uint16_t start;
    std::uint16_t count;              // registers or bits
    std::vector<std::size_t> tags;    // indices into the device's tag list
};

struct ScanGroup {
    std::chrono::milliseconds period;
    std::vector<ReadBlock> blocks;
};

// Groups tags by scan period, then per area merges contiguous or overlapping address ranges
// into blocks no larger than the per-request maximum. Deterministic output: groups by
// ascending period, blocks by (area, start). Pure function.
[[nodiscard]] std::vector<ScanGroup> plan_scan_groups(std::span<const config::TagConfig> tags);

} // namespace f2c::drivers::modbus
```

```cpp
// f2c/drivers/modbus/decode.hpp
#pragma once
#include <optional>
#include <span>
#include "f2c/config/plant_config.hpp"
#include "f2c/core/tag_sample.hpp"

namespace f2c::drivers::modbus {

// `words` begins at the tag's own address and holds at least width(type) registers.
// Unscaled integers → int64; float32 or any scaled value → double.
// Error (invalid_argument) for non-finite results; the caller turns it into bad quality.
[[nodiscard]] core::Expected<core::Value> decode_registers(
    const config::ModbusAddress& address,
    std::span<const std::uint16_t> words,
    const std::optional<config::LinearScaling>& scaling);

[[nodiscard]] core::Value decode_bit(bool bit) noexcept;

} // namespace f2c::drivers::modbus
```

```cpp
// f2c/drivers/modbus/poller.hpp
#pragma once
#include "f2c/core/backoff.hpp"
#include "f2c/core/clock.hpp"
#include "f2c/drivers/modbus/modbus_client.hpp"
#include "f2c/drivers/modbus/read_plan.hpp"

namespace f2c::drivers::modbus {

// Functional core of the Modbus driver: decides what to read and when.
// Owns no thread and no timer; the clock and the client are injected, so every
// scanning and reconnection rule is testable without a network or real time.
class Poller {
public:
    Poller(config::DeviceConfig device, ModbusClient& client, const core::Clock& clock);

    struct Result {
        std::vector<core::TagSample> samples;
        std::chrono::steady_clock::time_point next_wakeup;
    };

    // One step:
    //  - disconnected and backoff not elapsed → no I/O, next_wakeup = retry time;
    //  - disconnected → connect(); on failure emit bad samples once per outage, schedule retry;
    //    on success reset backoff and make every group due now;
    //  - connected → read every due block; good samples on success; bad samples for the block's
    //    tags on protocol_error; on io_error/timeout close, emit bad for all tags, schedule retry.
    // Group schedule is fixed-rate (due += period); if a group fell behind, it is rescheduled
    // to now + period (missed scans are skipped, never burst).
    Result poll();

    [[nodiscard]] bool connected() const noexcept;
    // …
};

} // namespace f2c::drivers::modbus
```

```cpp
// f2c/drivers/modbus/modbus_driver.hpp
#pragma once
#include <asio/any_io_executor.hpp>
#include "f2c/drivers/driver.hpp"
#include "f2c/drivers/modbus/modbus_client.hpp"
#include "f2c/drivers/registry.hpp"

namespace f2c::drivers::modbus {

// Thin shell: a steady_timer on the executor calls Poller::poll() and re-arms at next_wakeup.
class ModbusDriver final : public Driver {
public:
    ModbusDriver(asio::any_io_executor executor,
                 config::DeviceConfig device,
                 std::unique_ptr<ModbusClient> client,
                 const core::Clock& clock = core::system_clock());
    std::string_view id() const override;
    void start(SampleSink sink) override;
    void stop() override;
    DriverState state() const override;
    // …
};

// Registers type "modbus-tcp" (factory uses make_libmodbus_client).
core::Expected<void> register_modbus_tcp(DriverRegistry& registry);

} // namespace f2c::drivers::modbus
```

### 4.5 `libs/messaging`

```cpp
// f2c/messaging/topic.hpp
#pragma once
#include "f2c/core/tag_path.hpp"

namespace f2c::messaging {

inline constexpr std::string_view topic_prefix = "f2c/v1";

[[nodiscard]] std::string tag_topic(const core::TagPath& path);                     // f2c/v1/<path>
[[nodiscard]] core::Expected<core::TagPath> tag_path_from_topic(std::string_view topic);
[[nodiscard]] bool is_system_topic(std::string_view topic) noexcept;               // any segment starts with '_'
[[nodiscard]] std::string gateway_status_topic(std::string_view site, std::string_view client_id);
[[nodiscard]] std::string historian_subscription();                                 // "f2c/v1/#"

} // namespace f2c::messaging
```

```cpp
// f2c/messaging/json_codec.hpp
#pragma once
#include <optional>
#include "f2c/core/tag_sample.hpp"

namespace f2c::messaging {

// Payload format in spec § 6.2.
[[nodiscard]] std::string encode_sample(const core::TagSample& sample);
[[nodiscard]] core::Expected<core::TagSample> decode_sample(core::TagPath path, std::string_view payload);

enum class GatewayState { online, offline };
// {"state":"online","ts":"…","version":"0.1.0"}; ts omitted when nullopt (the last will).
[[nodiscard]] std::string encode_status(GatewayState state, std::optional<core::Timestamp> ts);

} // namespace f2c::messaging
```

```cpp
// f2c/messaging/mqtt_client.hpp
#pragma once
#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include "f2c/core/error.hpp"

namespace f2c::messaging {

struct MqttWill {
    std::string topic;
    std::string payload;
    int qos{1};
    bool retain{true};
};

struct MqttConnectOptions {
    std::string uri;
    std::string client_id;
    std::chrono::seconds keepalive{10};
    bool clean_session{true};
    std::optional<MqttWill> will;
    std::size_t max_buffered_messages{10'000};          // while disconnected; oldest dropped
    std::chrono::milliseconds reconnect_min{500};
    std::chrono::milliseconds reconnect_max{10'000};
};

struct MqttMessage {
    std::string topic;
    std::string payload;
    int qos;
    bool retained;
};

// Handlers run on a library thread; they must not block (post to your own executor).
struct MqttHandlers {
    std::function<void(bool session_present)> on_connected;   // after every successful (re)connect
    std::function<void(std::string_view cause)> on_connection_lost;
    std::function<void(MqttMessage)> on_message;
};

// Asynchronous client with automatic reconnection, including when the very first connect fails.
// All methods are thread-safe and non-blocking (except disconnect, bounded by its timeout).
class MqttClient {
public:
    virtual ~MqttClient() = default;
    virtual void connect() = 0;                                       // retries until disconnect()
    virtual void disconnect(std::chrono::milliseconds timeout) = 0;
    // Buffered while disconnected. Error only for invalid arguments or a full buffer policy failure.
    virtual core::Expected<void> publish(std::string topic, std::string payload, int qos, bool retain) = 0;
    // Remembered and re-applied after every reconnect.
    virtual core::Expected<void> subscribe(std::string filter, int qos) = 0;
};

[[nodiscard]] std::unique_ptr<MqttClient> make_paho_client(MqttConnectOptions options, MqttHandlers handlers);

} // namespace f2c::messaging
```

```cpp
// f2c/messaging/change_filter.hpp
#pragma once
#include <chrono>
#include <unordered_map>
#include <vector>
#include "f2c/core/tag_sample.hpp"

namespace f2c::messaging {

// Publish-on-change with a per-tag heartbeat. Pure, single-threaded.
class ChangeFilter {
public:
    using time_point = std::chrono::steady_clock::time_point;

    explicit ChangeFilter(std::chrono::seconds heartbeat);  // 0 disables heartbeats

    // Records the sample as the tag's latest. Returns true (and marks it published at `now`)
    // if it is the tag's first sample or its value or quality differs from the last published one.
    // Values compare exactly (no deadband in M1).
    bool offer(const core::TagSample& sample, time_point now);

    // Latest sample of every tag whose last publish is at least `heartbeat` old; marks them published.
    std::vector<core::TagSample> take_heartbeats(time_point now);

    [[nodiscard]] std::optional<time_point> next_heartbeat() const;
    // …
};

} // namespace f2c::messaging
```

```cpp
// f2c/messaging/sample_publisher.hpp
#pragma once
#include <asio/any_io_executor.hpp>
#include <span>
#include "f2c/core/clock.hpp"
#include "f2c/messaging/change_filter.hpp"
#include "f2c/messaging/mqtt_client.hpp"

namespace f2c::messaging {

struct PublisherOptions {
    int qos{1};
    std::chrono::seconds heartbeat{30};
};

// All state lives on `executor`. submit() is the only method callable from any thread.
class SamplePublisher {
public:
    SamplePublisher(asio::any_io_executor executor, MqttClient& client,
                    PublisherOptions options, const core::Clock& clock = core::system_clock());

    // Copies the samples, posts to the executor, stamps gateway_time, applies the ChangeFilter, publishes.
    void submit(std::span<const core::TagSample> samples);

    void start();  // arms the heartbeat timer
    void stop();   // cancels it; call from the executor
    // …
};

} // namespace f2c::messaging
```

### 4.6 `libs/storage`

```cpp
// f2c/storage/sample_row.hpp
#pragma once
#include <optional>
#include "f2c/core/tag_sample.hpp"

namespace f2c::storage {

struct SampleRow {
    core::Timestamp time;                 // source_time
    std::string tag_path;
    std::optional<double> value_num;      // bool → 0/1, int64 → double, double as is
    std::optional<std::string> value_text;  // string values
    std::int16_t quality;                 // quality_code()
    core::Timestamp gateway_time;
};

[[nodiscard]] std::int16_t quality_code(core::Quality q) noexcept;  // good 192, uncertain 64, bad 0
[[nodiscard]] SampleRow to_row(const core::TagSample& sample);

} // namespace f2c::storage
```

```cpp
// f2c/storage/sample_batcher.hpp
#pragma once
#include <chrono>
#include <deque>
#include <optional>
#include <vector>
#include "f2c/core/tag_sample.hpp"

namespace f2c::storage {

struct BatchLimits {
    std::size_t max_batch{500};
    std::chrono::milliseconds max_age{500};
    std::size_t max_buffered{100'000};
};

// Bounded FIFO with size/age flush rules. Pure, single-threaded.
class SampleBatcher {
public:
    using time_point = std::chrono::steady_clock::time_point;
    explicit SampleBatcher(BatchLimits limits);

    void push(core::TagSample sample, time_point now);           // drops the oldest when full
    [[nodiscard]] bool due(time_point now) const;                 // size >= max_batch or oldest age >= max_age
    [[nodiscard]] std::optional<time_point> deadline() const;     // when the oldest sample becomes due
    [[nodiscard]] std::vector<core::TagSample> front() const;     // up to max_batch, oldest first
    void pop_front(std::size_t n);                                 // after a successful write
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] std::uint64_t dropped() const noexcept;
    // …
};

} // namespace f2c::storage
```

```cpp
// f2c/storage/sample_writer.hpp
#pragma once
#include <memory>
#include <span>
#include "f2c/core/backoff.hpp"
#include "f2c/core/clock.hpp"
#include "f2c/storage/sample_batcher.hpp"

namespace f2c::storage {

class SampleWriter {
public:
    virtual ~SampleWriter() = default;
    // All-or-nothing; idempotent (duplicates are ignored). Blocking.
    virtual core::Expected<void> write(std::span<const core::TagSample> samples) = 0;
};

// libpqxx: connects lazily and reconnects after failures; one transaction per call;
// INSERT … ON CONFLICT (tag_path, time) DO NOTHING. Connect timeout 3 s, statement timeout 5 s.
[[nodiscard]] std::unique_ptr<SampleWriter> make_pg_writer(std::string conninfo);

// Functional core of the historian's write loop.
class BatchFlusher {
public:
    BatchFlusher(SampleBatcher& batcher, SampleWriter& writer,
                 core::BackoffPolicy retry, const core::Clock& clock);

    // Writes batches while due (unless in backoff). On failure keeps the batch, backs off.
    // Returns when to call again (nullopt = nothing buffered).
    std::optional<std::chrono::steady_clock::time_point> step();
    // …
};

} // namespace f2c::storage
```

### 4.7 Simulator

```cpp
// simulators/modbus-machine/src/filler_model.hpp
namespace f2c::sim {

enum class MachineState : std::uint8_t { stopped = 0, running = 1, faulted = 2 };

struct FillerOutputs {
    std::uint32_t counter;
    std::uint16_t speed;            // bottles/min
    std::uint16_t speed_setpoint;
    MachineState state;
    std::int16_t temperature_x10;   // 0.1 °C
    bool in_fault;
};

class FillerModel {
public:
    explicit FillerModel(std::uint32_t seed);
    void set_speed_setpoint(std::uint16_t bpm);  // clamped to 0..300
    void set_fault(bool forced);
    void set_counter(std::uint32_t counter);     // start from a given count (tests the 2³² wrap)
    void step(std::chrono::milliseconds dt);
    [[nodiscard]] FillerOutputs outputs() const;
    // …
};

} // namespace f2c::sim
```

```cpp
// simulators/modbus-machine/src/register_map.hpp
namespace f2c::sim {

struct RegisterImage {                       // the § 2.1.1 tables, host order
    std::array<std::uint16_t, 5> holding{};
    std::array<std::uint16_t, 1> input{};
    std::array<std::uint8_t, 1> coils{};
};

[[nodiscard]] RegisterImage to_registers(const FillerOutputs& outputs);
// Forwards the writable entries (holding 3, coil 0) of `current` that differ from `published`.
void apply_client_writes(FillerModel& model, const RegisterImage& current,
                         const RegisterImage& published);

} // namespace f2c::sim
```

```cpp
// simulators/modbus-machine/src/options.hpp, server.hpp
namespace f2c::sim {

struct SimulatorOptions {
    std::string bind_address{"0.0.0.0"};
    std::uint16_t port{502};
    std::chrono::milliseconds step_period{100};
    std::optional<std::uint32_t> seed;                // none: random, logged at startup
    std::optional<std::filesystem::path> health_file;
    std::string log_level{"info"};
    bool help{false};
};

[[nodiscard]] core::Expected<SimulatorOptions> parse_options(std::span<const std::string_view> args);

// `listening` gets the bound port (port 0 picks an ephemeral one) before the first request.
[[nodiscard]] core::Expected<void>
run_server(const SimulatorOptions& options, std::uint32_t seed, const std::atomic<bool>& stop,
           const std::function<void(std::uint16_t port)>& listening = {});

} // namespace f2c::sim
```

Targets: `f2c::sim` (model, register image, CLI parsing; pure, unit-tested), `f2c::sim_server` (the libmodbus loop, linkable so task 9's smoke test can run the simulator in-process) and the `f2c-modbus-machine` executable (`--bind`, `--port`, `--step-ms`, `--seed`, `--health-file`, `--log-level`).

`run_server` owns a `modbus_mapping_t` and runs a single-threaded `select` loop over the listening socket and every client (up to 32). The model is stepped every `step_period` (missed steps are skipped, not replayed). After each request it applies client writes to the model (`apply_client_writes`) and writes the outputs back into the mapping at once, so a write to a read-only register is overwritten before the next request. A client that stalls mid-request for 1 s is dropped. SIGINT/SIGTERM set the `stop` flag; `select` returns with `EINTR` and the loop exits. `select` never waits more than 100 ms, so `stop` set from another thread (an in-process test) is seen promptly too.

## 5. Plant configuration

### 5.1 Example (`config/plant1.yaml`)

```yaml
site: plant1

mqtt:
  uri: tcp://mosquitto:1883
  client_id: gateway-01
  qos: 1
  keepalive_s: 10
  heartbeat_s: 30

devices:
  - id: filler
    path: production/line1/cell-c/filler
    driver:
      type: modbus-tcp
      host: modbus-machine
      port: 502
      unit_id: 1
      timeout_ms: 500
      reconnect: { initial_ms: 500, max_ms: 10000 }
    scan_ms: 1000
    tags:
      - { name: counter,        area: holding, address: 0, type: uint32 }
      - { name: speed,          area: holding, address: 2, type: uint16 }
      - { name: speed_setpoint, area: holding, address: 3, type: uint16 }
      - { name: state,          area: holding, address: 4, type: uint16 }
      - { name: temperature,    area: input,   address: 0, type: int16, scale: 0.1 }
      - { name: in_fault,       area: coil,    address: 0, type: bool }
```

This produces three requests per scan: holding 0–4 (5 registers), input 0 (1), coil 0 (1 bit). The temperature tag path is `plant1/production/line1/cell-c/filler/temperature`.

### 5.2 Schema

| Key | Type | Req. | Default | Rules |
| --- | --- | --- | --- | --- |
| `site` | string | yes | — | valid path segment |
| `mqtt.uri` | string | yes | — | `tcp://host:port` (TLS out of scope) |
| `mqtt.client_id` | string | yes | — | valid path segment |
| `mqtt.qos` | int | no | 1 | 0, 1 or 2 |
| `mqtt.keepalive_s` | int | no | 10 | 1–3600 |
| `mqtt.heartbeat_s` | int | no | 30 | 0 (off) or 1–86400 |
| `devices` | list | yes | — | at least one |
| `devices[].id` | string | yes | — | valid path segment; unique |
| `devices[].path` | string | yes | — | valid TagPath, relative to `site`; `site/path` unique |
| `devices[].scan_ms` | int | no | 1000 | 50–3 600 000 |
| `devices[].driver.type` | string | yes | — | registered driver type; M1: `modbus-tcp` |
| `…driver.host` | string | yes | — | non-empty |
| `…driver.port` | int | no | 502 | 1–65535 |
| `…driver.unit_id` | int | no | 1 | 0–255 |
| `…driver.timeout_ms` | int | no | 500 | 10–60 000; warning if ≥ smallest scan period |
| `…driver.byte_order` / `word_order` | enum | no | `big` | `big`, `little` |
| `…driver.reconnect.initial_ms` / `max_ms` | int | no | 500 / 10 000 | 10 ≤ initial ≤ max ≤ 600 000 |
| `devices[].tags` | list | yes | — | at least one |
| `tags[].name` | string | yes | — | valid path segment; unique in the device |
| `tags[].area` | enum | yes | — | `coil`, `discrete_input`, `holding`, `input` |
| `tags[].address` | int | yes | — | 0–65535; `address + width − 1` ≤ 65535 |
| `tags[].type` | enum | yes | — | `bool` only for `coil`/`discrete_input`; `int16`, `uint16`, `int32`, `uint32`, `float32` only for `holding`/`input` |
| `tags[].scale` / `offset` | number | no | 1 / 0 | numeric types only; finite; `scale ≠ 0`; either key enables scaling |
| `tags[].scan_ms` | int | no | device `scan_ms` | 50–3 600 000 |
| `tags[].byte_order` / `word_order` | enum | no | device value | `word_order` only meaningful for 32-bit types |

Validation rules beyond the table:

- **Unknown keys are errors**, with a "did you mean" suggestion when the edit distance is ≤ 2 (`scan_mss` → `scan_ms`). Typos must not silently fall back to defaults. A misspelled required key is reported once (as unknown, with the suggestion), not also as missing.
- A key given twice in the same mapping is an error.
- Integers and numbers must be plain YAML scalars: `port: "502"` (quoted) is a type error. A leading `+` is accepted.
- When `driver.type` is missing or unknown, the driver's other keys and the tags' driver-specific keys (`area`, `address`, …) are not checked: the type error is the one to fix.
- Warnings (currently only `timeout_ms` ≥ smallest scan period) do not prevent loading; they are returned in `LoadedConfig::warnings` with the same location format.
- All errors are reported in one run, each as `<file>:<line>:<col>: <key path>: <message>`.
- Overlapping tags at the same address are allowed (e.g. a uint32 and its low word as uint16).
- Byte/word order semantics, for a 32-bit value `0xAABBCCDD` stored in registers R0, R1: big/big = `AABB CCDD` ("ABCD"), big/little = `CCDD AABB` ("CDAB"), little/big = `BBAA DDCC` ("BADC"), little/little = `DDCC BBAA` ("DCBA").

## 6. MQTT contract

Provisional: Milestone 2 replaces it with Sparkplug B ([ADR-0008](../adr/0008-provisional-json-mqtt-contract.md)).

### 6.1 Topics

| Topic | Publisher | QoS | Retained | Payload |
| --- | --- | --- | --- | --- |
| `f2c/v1/<tag path>` | gateway | `mqtt.qos` (1) | no | tag sample |
| `f2c/v1/<site>/_gateway/<client_id>/status` | gateway | 1 | yes | gateway status |

- `<tag path>` is the full TagPath including the site: `f2c/v1/plant1/production/line1/cell-c/filler/temperature`.
- `v1` is the contract version. Any incompatible change before Sparkplug B bumps it.
- Segments starting with `_` are reserved for system topics; TagPath can never produce them, so tag and system topics never collide.

### 6.2 Tag sample payload

UTF-8 JSON object:

| Key | Type | Meaning |
| --- | --- | --- |
| `v` | `null`, boolean, integer, number or string | Value. A JSON number without fraction or exponent is an integer (int64); otherwise a double. `null` = no value. |
| `q` | `"good"`, `"uncertain"`, `"bad"` | Quality. |
| `ts` | RFC 3339 string | Source time, UTC, millisecond precision. |
| `gts` | RFC 3339 string | Gateway time, UTC, millisecond precision. |

Examples:

```json
{"v": 41.7, "q": "good", "ts": "2026-10-20T13:05:01.120Z", "gts": "2026-10-20T13:05:01.123Z"}
{"v": 18342, "q": "good", "ts": "2026-10-20T13:05:01.120Z", "gts": "2026-10-20T13:05:01.123Z"}
{"v": true, "q": "good", "ts": "2026-10-20T13:05:01.120Z", "gts": "2026-10-20T13:05:01.123Z"}
{"v": 41.7, "q": "bad", "ts": "2026-10-20T13:05:03.640Z", "gts": "2026-10-20T13:05:03.641Z"}
{"v": null, "q": "bad", "ts": "2026-10-20T13:05:00.010Z", "gts": "2026-10-20T13:05:00.011Z"}
```

Rules:

- Encoders always write a `double` with a fraction or exponent (`42.0`, never `42`), so the type round-trips.
- Decoders ignore unknown keys (forward compatibility) and reject: missing `v`/`q`/`ts`/`gts`, integers outside int64, unknown quality strings, malformed timestamps, non-object payloads.
- A `bad` sample carries the last known value if there is one, else `null`.

### 6.3 Publishing rules

- A sample is published when it is the tag's first, or its value or quality differs from the last **published** sample of that tag.
- Heartbeat: if a tag has not been published for `heartbeat_s`, its **latest** sample (which may be newer than the last published one, with the same value) is republished with a fresh `gts`. If the driver is disconnected the latest sample is the `bad` one, whose `ts` does not change; the database ignores the duplicate (HIS-4).
- Order is preserved per tag (single publisher strand, single MQTT connection, QoS 1).

### 6.4 Gateway status payload

```json
{"state": "online", "ts": "2026-10-20T13:05:00.000Z", "version": "0.1.0"}
{"state": "offline", "ts": "2026-10-20T14:00:00.000Z", "version": "0.1.0"}   // clean shutdown
{"state": "offline", "version": "0.1.0"}                                     // last will (no ts: fixed at connect time)
```

Known limitation: if the gateway dies while the broker is down, the will is never published and the retained status stays `online`. Accepted in M1.

### 6.5 Broker configuration (Mosquitto 2)

`listener 1883`, `allow_anonymous true` (no auth in M1), `persistence true`, `max_queued_messages 100000` (the historian's persistent session can queue during its outages), `max_inflight_messages 100`.

## 7. Database schema

`deploy/db/001_init.sql`, run once by the TimescaleDB image's `docker-entrypoint-initdb.d`:

```sql
CREATE EXTENSION IF NOT EXISTS timescaledb;

CREATE TABLE tag_samples (
    time         TIMESTAMPTZ      NOT NULL,   -- source time (µs precision; payload carries ms)
    tag_path     TEXT             NOT NULL,   -- full TagPath incl. site
    value_num    DOUBLE PRECISION,            -- bool (0/1), integers, floats
    value_text   TEXT,                        -- strings
    quality      SMALLINT         NOT NULL,   -- 192 good, 64 uncertain, 0 bad
    gateway_time TIMESTAMPTZ      NOT NULL,
    CONSTRAINT tag_samples_pkey PRIMARY KEY (tag_path, time),
    CONSTRAINT tag_samples_quality_chk CHECK (quality IN (0, 64, 192)),
    CONSTRAINT tag_samples_one_value_chk CHECK (value_num IS NULL OR value_text IS NULL)
);

SELECT create_hypertable('tag_samples', by_range('time', INTERVAL '1 day'));
SELECT add_retention_policy('tag_samples', INTERVAL '30 days');

-- Grafana reads with this role.
CREATE ROLE grafana_reader LOGIN PASSWORD 'grafana';
GRANT SELECT ON tag_samples TO grafana_reader;
```

- The primary key `(tag_path, time)` replaces the plan's `(tag_path, time DESC)` index: it serves the same queries (Postgres scans B-trees backwards) and gives idempotent inserts (`ON CONFLICT DO NOTHING`).
- Insert statement (one per batch, inside one transaction):

  ```sql
  INSERT INTO tag_samples (time, tag_path, value_num, value_text, quality, gateway_time)
  SELECT * FROM unnest($1::timestamptz[], $2::text[], $3::float8[], $4::text[], $5::int2[], $6::timestamptz[])
  ON CONFLICT (tag_path, time) DO NOTHING;
  ```

- Credentials (`f2c`/`f2c`, `grafana_reader`/`grafana`) are demo-only and live in the Compose file.
- Schema migrations beyond this init script are out of scope; a migration tool gets an ADR when the schema first changes.

Typical Grafana query:

```sql
SELECT time, value_num AS temperature
FROM tag_samples
WHERE tag_path = 'plant1/production/line1/cell-c/filler/temperature'
  AND quality = 192 AND $__timeFilter(time)
ORDER BY time;
```

## 8. Deployment

### 8.1 Compose services (`deploy/compose/docker-compose.yml`)

| Service | Image | Host port | Healthcheck | Depends on (healthy) |
| --- | --- | --- | --- | --- |
| `modbus-machine` | built (`deploy/docker/Dockerfile`, target `modbus-machine`) | 1502 → 502 | health file fresh | — |
| `mosquitto` | `eclipse-mosquitto:2` (pinned patch) | 1883 | `mosquitto_sub -t '$SYS/broker/uptime' -C 1 -W 3` | — |
| `timescaledb` | `timescale/timescaledb:2.x-pg16` (pinned) | `${F2C_PG_PORT:-5432}` | `pg_isready` | — |
| `gateway` | built, target `gateway` | — | health file fresh | mosquitto, modbus-machine |
| `historian-writer` | built, target `historian-writer` | — | health file fresh | mosquitto, timescaledb |
| `grafana` | `grafana/grafana-oss` (pinned) | 3000 | `/api/health` | timescaledb |

- One multi-stage Dockerfile: a builder stage (Ubuntu 24.04, GCC 13, vcpkg at the manifest baseline, BuildKit cache mount for vcpkg binaries) builds all targets once; slim runtime stages copy one binary each.
- Health file: each C++ service touches `/tmp/f2c-healthy` every 5 s while its main loop runs (`--health-file`); the check is `test $(( $(date +%s) - $(stat -c %Y /tmp/f2c-healthy) )) -lt 15`. Health means "the process is alive and its loop turns", not "its dependencies are up" — otherwise a broker outage would cascade into restarts.
- `restart: unless-stopped` on all services. (The integration test checks container start time, so restarts cannot hide crashes.)
- Grafana: anonymous viewer access enabled, `GF_DASHBOARDS_MIN_REFRESH_INTERVAL=1s`, datasource and dashboard provisioned from `deploy/grafana/`.

### 8.2 Dashboard "Cell C — Filler"

Refresh 1 s, default range last 15 min. Panels: temperature (time series), speed and setpoint (time series), counter and bottles/min derived from it, machine state (state timeline), fault (state timeline), data quality (table: latest quality and age per tag).

### 8.3 Historian configuration

| Variable | Default |
| --- | --- |
| `F2C_MQTT_URI` | `tcp://mosquitto:1883` |
| `F2C_MQTT_CLIENT_ID` | `historian-01` |
| `F2C_DB_URI` | `postgresql://f2c:f2c@timescaledb:5432/f2c` |
| `F2C_BATCH_MAX_SAMPLES` | 500 |
| `F2C_BATCH_MAX_AGE_MS` | 500 |
| `F2C_BUFFER_MAX_SAMPLES` | 100000 |
| `F2C_LOG_LEVEL` | `info` |

Invalid values → exit code 1 with a message naming the variable.

### 8.4 Process conventions

Exit codes: 0 clean shutdown, 1 configuration error, 2 fatal runtime error. Logs go to stdout (Docker collects them), pattern `%Y-%m-%dT%H:%M:%S.%e %^%l%$ [%n] %v`.

### 8.5 Build

- `CMakePresets.json`: `dev` (Debug, default compiler), `release`, `asan` (Clang, ASan + UBSan), `ci-gcc`, `ci-clang`. Toolchain from `$env{VCPKG_ROOT}`.
- `vcpkg.json` with a pinned `builtin-baseline`: asio, tl-expected, spdlog, yaml-cpp, libmodbus, paho-mqttpp3, nlohmann-json, libpqxx, catch2, trompeloeil.
- Warnings as errors on every target through a shared `f2c_warnings` interface target.
- vcpkg builds libmodbus with autotools, so the build host needs `autoconf autoconf-archive automake libtool` from the system package manager (CI and the Docker builder install them).

## 9. Test plan

### 9.1 Approach

- **TDD** per task: each test below is written before the code that passes it.
- **Unit tests** (Catch2 v3, label `unit`) cover the pure cores exhaustively. No network, no Docker, no `sleep`: time comes from `core::testing::ManualClock` or explicit `now` arguments.
- **Test doubles**: hand-written fakes for stateful collaborators (`FakeModbusClient` with a register map and failure injection, `FakeSampleWriter`, `FakeMqttClient` recording publishes); trompeloeil mocks where the interaction itself is the assertion (e.g. "publishes exactly once with retain = true").
- **Shells** (`ModbusDriver`, `SamplePublisher`, `PahoMqttClient`, `PgSampleWriter`, `HistorianPipeline`) are covered by the integration test plus a few cheap tests against the real library:
  - **Component tests** (label `component`, run in CI): everything inside the test process, e.g. the simulator server or `LibmodbusClient` against the simulator running in-process on an ephemeral loopback port. They check only what unit tests cannot (wiring, wire format, connection handling), never the pure logic again. They may poll with a deadline, never a bare sleep.
  - **Local smoke tests** (label `local`, run by hand): need a service outside the test process, e.g. `PgSampleWriter` against a local TimescaleDB.
- See [ADR-0003](../adr/0003-catch2-trompeloeil.md) for the labels.
- **Integration tests** (pytest, ADR-0012) run against the Compose stack; they map one-to-one to the acceptance criteria.

### 9.2 Unit tests

**core** (`[core]`)

- TagPath: accepts the plan's example; rejects empty string, empty segment (`a//b`, leading/trailing `/`), uppercase, `+`, `#`, spaces, segment starting with `_`, segment of 65 chars, 17 segments; `append`/`join` keep invariants; equality, ordering, hashing.
- Timestamp: format truncates to ms (`.1209999` → `.120`); format of epoch and of a pre-epoch instant; parse `Z`, `+02:00`, a negative offset crossing midnight, lowercase `t`/`z`, leap day, 0, 1 and 9 fractional digits; round-trip at ms precision; rejects missing `T`, bad month, invalid day (Feb 30, Feb 29 in a non-leap year), out-of-range time and offset, leap second, empty or 10-digit fraction, trailing characters, garbage.
- Quality: to_string/parse round-trip; unknown string → error.
- Backoff: sequence 500, 1000, 2000, …, capped at max; reset; multiplier 1.0 → constant; fractional multiplier; initial = max → constant.
- ManualClock: `advance` moves both clocks.

**config** (`[config]`)

- The example file loads into the expected `PlantConfig` (paths resolved with site, defaults filled, per-tag overrides resolved).
- One test per schema rule: missing required key, out-of-range value, wrong type (`port: "abc"`), invalid segment, duplicate device id, duplicate tag name, duplicate device path, `bool` on holding, `uint16` on coil, scale on bool, `address + width` overflow, unknown driver type, unknown key with suggestion.
- Multiple errors in one file are all reported; each has correct line/column and key path.
- `format_errors` output format.
- Missing file and YAML syntax errors produce a `ConfigError`, not an exception.

**drivers** (`[drivers]`)

- Registry: create known type; unknown type → `not_found`; duplicate `add` → error; factory error propagates.

**drivers/modbus** (`[modbus]`)

- `plan_scan_groups`: single tag; contiguous merge; overlapping merge (uint32 at 0 + uint16 at 1); gap → two blocks; split at 125 registers / 2000 bits; different areas never merge; different periods → different groups; deterministic order; tag indices correct; the § 5.1 example → exactly 3 blocks.
- `decode_registers`: every type × every byte/word order with known vectors (int16 −1, uint16 65535, int32 min/max, uint32 0xAABBCCDD, float32 1.5 and −0.0); scaling (`int16 417 × 0.1 = 41.7`, offset); unscaled integers are `int64`; NaN/Inf → error.
- `Poller` with `FakeModbusClient` and `ManualClock`:
  - first poll connects and reads every group; samples are `good` with `source_time` = clock time;
  - next wakeup equals the earliest group deadline; groups with different periods are read at their own rates;
  - fixed-rate scheduling; a group that falls behind skips missed scans (no burst);
  - initial connect failure → one set of `bad` samples with `monostate`, retry at backoff delay, no I/O before it;
  - read timeout while running → client closed, all tags `bad` with last value, once; backoff grows 500 → 1000 → 2000 on repeated failures; resets after success;
  - recovery → `good` samples again on the first poll after reconnect;
  - Modbus exception on one block → only its tags `bad`, connection kept, other blocks `good`;
  - decode error on one tag → only that tag `bad`;
  - **criterion 3 at unit level**: with 1 s scan and 500 ms timeout, tags turn `bad` ≤ 2 s after the fake device stops responding.

**messaging** (`[messaging]`)

- Topic: `tag_topic` of the example; `tag_path_from_topic` round-trip; wrong prefix, wrong version, empty → error; `is_system_topic` for status topic; status topic format.
- JSON codec: encode each value type (incl. `double 42.0` → `42.0`, `monostate` → `null`); decode each; round-trip; unknown keys ignored; every rejection rule in § 6.2; status encoding with and without `ts`.
- ChangeFilter: first sample published; same value and quality suppressed; value change published; quality change with same value published; heartbeat returns the latest (not last-published) sample after the interval and not before; heartbeat 0 → never; `next_heartbeat`; independent tags don't interfere.
- SamplePublisher (with `FakeMqttClient`, `asio::io_context` run manually): `submit` from another thread ends up published on the executor; `gateway_time` stamped from the clock; topic, QoS and payload correct; suppressed duplicates not published.

**storage** (`[storage]`)

- `to_row`: bool → 0/1, int64, double, string → `value_text`, monostate → both null; quality codes.
- SampleBatcher: due at `max_batch`; due at `max_age` of the oldest; not due otherwise; `deadline`; `front` returns ≤ `max_batch` oldest-first; `pop_front`; overflow drops oldest and counts.
- BatchFlusher with `FakeSampleWriter` and `ManualClock`: writes when due; nothing when not due; failure keeps the batch and returns a backoff deadline; no write attempt before it; success after failure writes the same batch then continues; backoff resets; buffer bound respected during a long outage.

**simulator** (`[sim]`)

- FillerModel: initial state running toward 120 bpm; speed ramps at 10 bpm/s; counter integrates speed (120 bpm for 60 s → 120 ± 1); setpoint 0 → stopped; fault → speed 0, state faulted, counter frozen; clearing fault resumes; temperature stays within plausible bounds and same seed → same sequence; setpoint clamped to 300.

### 9.3 Process-level and component tests (no Docker)

Process-level (label `unit`):


- `f2c-gateway --config <invalid file>` exits 1 and prints all errors (CTest with `PASS_REGULAR_EXPRESSION`).
- `f2c-historian-writer` with `F2C_BATCH_MAX_SAMPLES=abc` exits 1 naming the variable.

Component (label `component`, `[sim][server]`): `run_server` on an ephemeral loopback port, driven by a libmodbus client, with a 60 s step period so the model does not move during a test:

- reports the bound port; serves holding 0–4, input 0 and coil 0 with the initial values;
- a setpoint write reaches the model (and is clamped to 300); writes to read-only registers are overwritten at once;
- the fault coil sets and clears state `faulted`;
- an address outside the map → exception "illegal data address", connection kept;
- two interleaved clients see each other's writes; a new client is served after another disconnects;
- touches the health file; stops within 1 s of `stop` with a client connected; port in use → `io_error`.

### 9.4 Integration tests (`tests/integration`, pytest)

A Python 3.12 pytest suite ([ADR-0012](../adr/0012-integration-tests-pytest.md)), black-box against the Compose stack:

```text
tests/integration/
├── requirements.txt     # pinned: pytest, pymodbus, psycopg[binary], paho-mqtt, requests
├── conftest.py          # session fixture: compose up --build --wait, logs on failure, down
├── helpers/             # compose wrapper, DB polling, Modbus and MQTT helpers
└── test_*.py            # one module per acceptance criterion
```

- `pytest tests/integration` brings the stack up itself (session-scoped fixture running `docker compose up -d --build --wait`), dumps `docker compose logs` into the report when a test fails, and tears down at the end. `--keep-stack` skips the teardown when debugging; `F2C_STACK_RUNNING=1` reuses a stack that is already up.
- Tests talk only to exposed ports: Modbus `localhost:1502` (pymodbus), PostgreSQL (psycopg), MQTT `localhost:1883` (paho-mqtt), Grafana `localhost:3000` (requests). Docker is driven through the `docker compose` CLI.
- Waiting is always "poll until condition or deadline" (helper `wait_until(predicate, timeout, interval)`), never a bare sleep.
- Tests that disturb the stack (IT-3 … IT-6) restore it before returning, so modules can run in any order.

| Test | Acceptance criterion | Procedure | Pass condition |
| --- | --- | --- | --- |
| IT-1 stack up | 1 | After `--wait`: query Grafana `/api/health`, `/api/search?query=Filler`; query DB | Grafana healthy, dashboard provisioned, rows for all 6 tags with quality 192 within 10 s |
| IT-2 latency | 2 | Record T0, write coil 0 = true on `localhost:1502`; poll DB every 50 ms | Row `…/in_fault` with `value_num = 1` visible at T < T0 + 2 s. Clear the fault afterwards. |
| IT-3 device loss | 3 | `docker compose stop modbus-machine`, record T0; poll DB; then `start`, record T1 | Bad row for every filler tag with `time` ≤ T0 + 2 s (+ 0.5 s tolerance for the stop itself); good rows again ≤ T1 + 12 s (backoff cap 10 s) |
| IT-4 broker restart | 4 | Record gateway/historian `State.StartedAt`; `docker compose restart mosquitto`; wait | `StartedAt` unchanged for both; new good rows appear ≤ 15 s after the broker is healthy |
| IT-5 gateway killed | 5 | Subscribe to the status topic; `docker compose kill -s SIGKILL gateway` | Retained status becomes `offline` within 1.5 × keepalive + 2 s (17 s) |
| IT-6 clean shutdown | (GW-12) | `docker compose stop gateway` | Status `offline` with `ts`, published before the container exits; exit code 0 |
| IT-7 duplicates | (HIS-4) | Publish the same sample payload twice to a test tag topic | Exactly one row |

Criterion 6 (green CI) is the CI pipeline itself.

### 9.5 CI (GitHub Actions)

| Job | Content |
| --- | --- |
| `format` | `clang-format --dry-run -Werror` on all C++ files |
| `build-test` (matrix gcc-13, clang-18) | configure with `ci-gcc`/`ci-clang`, build, `ctest -L unit`, `ctest -L component` |
| `asan` | `asan` preset, `ctest -L unit`, `ctest -L component` |
| `clang-tidy` | `clang-tidy` with the project `.clang-tidy`, warnings as errors, using the clang build's `compile_commands.json` |
| `integration` | `actions/setup-python` 3.12, `pip install -r tests/integration/requirements.txt`, `pytest tests/integration --junitxml=…` on `ubuntu-24.04` (needs `build-test`) |

vcpkg binary cache via `actions/cache` (files provider); Docker layer cache via BuildKit `type=gha`.

## 10. Deviations from the project plan

| Plan | This spec | Why |
| --- | --- | --- |
| `Value = variant<bool, int64, double, string>` | Adds `std::monostate` first | A `bad` sample of a never-read tag needs "no value"; maps to JSON `null` and SQL `NULL`. |
| `register_driver(type, factory)` free function | `DriverRegistry` instance, explicit registration in `main` | No global mutable state; testable; static-registration tricks are dropped by the linker with static libraries (ADR-0007). |
| Factory returns `unique_ptr<Driver>` | Returns `Expected<unique_ptr<Driver>>` | Factories can fail (bad driver config) without exceptions (ADR-0011). |
| Layout: `libs/core, config, drivers, messaging` | Adds `libs/storage` | Historian logic (batching, row mapping, retry) belongs in a testable library, per the thin-services rule. |
| Index `(tag_path, time DESC)` | Primary key `(tag_path, time)` | Same queries, plus idempotent inserts for QoS 1 duplicates. |
| Config example | Adds `keepalive_s`, `heartbeat_s`, `timeout_ms`, `reconnect`, byte/word order, per-tag `scan_ms`, `offset`; adds `speed_setpoint` and `state` tags | Requirements in the plan (heartbeat, backoff, byte/word order, per-group period) need these knobs; the extra tags exercise read merging. |
| TagPath: "allowed characters only" | Lowercase `[a-z0-9_-]`, `_` prefix reserved | Avoids case-only duplicates; keeps a collision-free namespace for system topics. |
| Status payload unspecified | JSON `{"state", "ts", "version"}` | Consistent with tag payloads; `ts` tells when a clean shutdown happened. |

## 11. Open decisions

All resolved on 2026-10-18: every recommendation accepted, except D9 (pytest instead of Catch2). The ADRs are **Accepted**.

| # | Decision | Options | Outcome | Record |
| --- | --- | --- | --- | --- |
| D1 | MQTT client library | Paho C++ · Boost.MQTT5 | **Paho C++**, hidden behind `MqttClient` | [ADR-0006](../adr/0006-mqtt-client-library.md) |
| D2 | Gateway concurrency model | One `io_context` + thread per driver · shared pool | **One per driver**; publisher on the main `io_context` | [ADR-0010](../adr/0010-gateway-concurrency-model.md) |
| D3 | Asio flavour | Standalone Asio · Boost.Asio | **Standalone** (`asio` vcpkg port); revisit if Boost.MQTT5 or Beast (M6) is adopted | ADR-0010 |
| D4 | Error handling | Exceptions · `tl::expected` · error codes | **`tl::expected` aliased as `core::Expected`** | [ADR-0011](../adr/0011-error-handling.md) |
| D5 | Where driver-specific config is parsed | Closed `std::variant` in `libs/config` · opaque node parsed by each driver | **Closed variant** for now | [ADR-0007](../adr/0007-driver-plugin-model.md) |
| D6 | "No value" in `Value` | `std::monostate` · `optional<Value>` in `TagSample` · don't emit until first read | **`std::monostate`** | ADR-0008, § 10 |
| D7 | Quality encoding in the DB | 192/64/0 (OPC classic) · 0/1/2 · text | **192/64/0**: familiar to industrial readers, room for substatus later | [ADR-0009](../adr/0009-timescaledb-historian.md) |
| D8 | Historian configuration | Env vars · its own YAML · section in the plant file | **Env vars**: the plant file describes the plant, not the services; env fits Compose | § 8.3 |
| D9 | Integration test tooling | Catch2 (C++) · pytest | **pytest** (decided; overrides the Catch2 recommendation): shorter Docker orchestration and polling, mature clients for Modbus, MQTT and PostgreSQL. Catch2 remains the rule for all C++ tests. | [ADR-0012](../adr/0012-integration-tests-pytest.md) |
| D10 | How Compose gets images | Build locally from source · pull prebuilt images from GHCR | **Build locally** in M1 (first build is slow, ~10–20 min, cached after). Publishing images to GHCR can come with the release workflow. | § 8.1 |
| D11 | vcpkg triplet / linkage | Static (`x64-linux`) · dynamic | **Static**. libmodbus is LGPL-2.1: static linking is compliant here because the full source of floor2cloud is public and relinkable. Noted in ADR-0005. | ADR-0005 |
| D12 | Heartbeat default | 30 s · 60 s | **30 s** | § 5.2 |

Not decided here (later milestones): Sparkplug B implementation, Kafka/Redpanda, schema migration tool, jitter in backoff (needed once many gateways reconnect at once).

## 12. Task order

One task per pull request, tests first. Each task leaves `main` green.

| # | Task | Depends on | Done when |
| --- | --- | --- | --- |
| 0 | This spec and the initial ADRs | — | ✅ Done: spec accepted, decisions resolved, ADRs Accepted |
| 1 | Repository skeleton: `LICENSE`, `CMakeLists.txt`, `CMakePresets.json`, `vcpkg.json`, `.clang-format`, `.clang-tidy`, `f2c_warnings`, Catch2 + CTest with one trivial test | 0 | `cmake --preset dev && cmake --build --preset dev && ctest --preset dev` passes on GCC and Clang |
| 2 | CI: `format`, `build-test` matrix, `asan`, `clang-tidy` jobs | 1 | Green on a PR |
| 3 | `libs/core`: Error, TagPath, Timestamp, TagSample, Quality, Clock + ManualClock, Backoff | 1 | § 9.2 core tests pass |
| 4 | `libs/config`: structs, YAML loader, validation, error formatting, `config/plant1.yaml` | 3 | § 9.2 config tests pass |
| 5 | `simulators/modbus-machine`: FillerModel (tests first), libmodbus server, Dockerfile target | 1 | Model and component tests pass; `mbpoll`/libmodbus client reads the register map from the container |
| 6 | `libs/drivers`: Driver, DriverRegistry | 4 | Registry tests pass |
| 7 | `libs/drivers/modbus` part 1: `plan_scan_groups`, `decode_*` | 6 | Plan and decode tests pass |
| 8 | `libs/drivers/modbus` part 2: `ModbusClient`, `FakeModbusClient`, `Poller` | 7 | Poller tests pass |
| 9 | `libs/drivers/modbus` part 3: `LibmodbusClient`, `ModbusDriver` shell, `register_modbus_tcp` | 8, 5 | Local smoke test against the in-process simulator |
| 10 | `libs/messaging` part 1: topic, JSON codec, ChangeFilter | 3 | Tests pass |
| 11 | `libs/messaging` part 2: `MqttClient`, `PahoMqttClient`, `SamplePublisher` | 10 | SamplePublisher tests pass; manual check against a local Mosquitto |
| 12 | `services/gateway`: CLI, config load, registry, drivers, publisher, status + will, signals, health file, logging | 9, 11 | Process-level config test passes; runs against simulator + Mosquitto and `mosquitto_sub` shows the contract |
| 13 | `deploy/db`: init script | 0 | Applies cleanly on the pinned TimescaleDB image |
| 14 | `libs/storage`: `to_row`, SampleBatcher, BatchFlusher, `PgSampleWriter` | 3, 13 | Unit tests pass; writer smoke test against a local TimescaleDB (`local` label) |
| 15 | `services/historian-writer`: env config, subscription, decode, pipeline, signals, health file | 10, 11, 14 | Process-level env test passes |
| 16 | `deploy/compose` (+ `deploy/docker/Dockerfile`, Mosquitto config) with healthchecks | 5, 12, 13, 15 | `docker compose up` brings all services to healthy |
| 17 | `deploy/grafana`: datasource and dashboard | 16 | Dashboard shows live data anonymously |
| 18 | Integration tests IT-1…IT-7 (pytest suite, fixtures, helpers) + CI `integration` job | 16, 17 | `pytest tests/integration` green locally and in CI |
| 19 | README: diagram, quick start, demo GIF; update CLAUDE.md status | 18 | Milestone 1 acceptance criteria all met |

Tasks 5, 10 and 13 have no dependency on the driver chain and can be done in any order after their prerequisites.
