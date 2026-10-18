# 0007. Driver plug-in model: common interface and registry

Status: Accepted · Date: 2026-10-18

## Context

The gateway will speak Modbus TCP (Milestone 1), then OPC UA, ADS and S7 (Milestone 2). Each protocol has its own library, threading behaviour and addressing scheme. The gateway must pick drivers from the plant configuration and keep protocol code separate from publishing.

## Decision

- **Interface.** Every driver implements `f2c::drivers::Driver` (`id`, `start(SampleSink)`, `stop`, `state`). A driver only reads its protocol and hands normalized `core::TagSample`s to the sink; it knows nothing about MQTT. `stop()` guarantees no sink call after it returns.
- **Registry instance.** `DriverRegistry` maps a type string (`"modbus-tcp"`) to a factory `(executor, DeviceConfig) → Expected<unique_ptr<Driver>>`. Each service creates a registry and registers the drivers it links explicitly (`register_modbus_tcp(registry)`) in `main`. No global registry, no self-registration through static initializers — those are silently dropped when linking static libraries and make tests order-dependent.
- **Compile-time plug-ins.** Drivers are libraries linked into the gateway, not runtime-loaded shared objects.
- **Configuration.** Driver-specific settings are a closed `std::variant` in `libs/config` (`DriverConfig = variant<ModbusTcpConfig>`, `TagAddress = variant<ModbusAddress>`). The config loader validates everything before any driver starts, so errors are reported together with file positions.

## Consequences

- Adding a protocol = a new library, a new variant alternative in `libs/config`, and one `register_…` call. The config library changes with each protocol; with four protocols planned this is acceptable and keeps validation central.
- If third parties ever need out-of-tree drivers, the variant becomes an opaque per-driver node parsed by a parser registered alongside the factory, and runtime loading can be added. Neither is needed now.
- The factory signature carries the executor, which ties the interface to Asio (ADR-0010).
