#pragma once

#include "f2c/core/backoff.hpp"
#include "f2c/core/tag_path.hpp"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace f2c::config {

// Every field is resolved by the loader: defaults filled, per-tag overrides applied. Nothing
// downstream reads YAML (ADR-0004).

struct MqttConfig {
    std::string uri;       // tcp://host:port
    std::string client_id; // a valid TagPath segment (used in the status topic)
    int qos{1};            // 0..2
    std::chrono::seconds keepalive{10};
    std::chrono::seconds heartbeat{30}; // 0 disables
};

enum class ModbusArea : std::uint8_t { coil, discrete_input, holding, input };
enum class ModbusType : std::uint8_t { boolean, int16, uint16, int32, uint32, float32 };
enum class ByteOrder : std::uint8_t { big, little }; // bytes inside each 16-bit register
enum class WordOrder : std::uint8_t { big, little }; // 32-bit register order; big = high first

struct ModbusTcpConfig {
    std::string host;
    std::uint16_t port{502};
    std::uint8_t unit_id{1};
    std::chrono::milliseconds timeout{500};
    core::BackoffPolicy reconnect{};
    ByteOrder byte_order{ByteOrder::big}; // device defaults, overridable per tag
    WordOrder word_order{WordOrder::big};
};

struct ModbusAddress {
    ModbusArea area;
    std::uint16_t address;
    ModbusType type;
    ByteOrder byte_order; // resolved: tag override or device default
    WordOrder word_order;
};

// Closed sets: Milestone 2 adds OPC UA, ADS and S7 alternatives (ADR-0007).
using DriverConfig = std::variant<ModbusTcpConfig>;
using TagAddress = std::variant<ModbusAddress>;

struct LinearScaling {
    double scale{1.0};
    double offset{0.0};
};

struct TagConfig {
    std::string name;                      // last path segment
    core::TagPath path;                    // full: site/device path/name
    std::chrono::milliseconds scan_period; // resolved: tag override or device default
    std::optional<LinearScaling> scaling;
    TagAddress address;
};

struct DeviceConfig {
    std::string id;     // unique in the file; used for logs and Driver::id()
    core::TagPath path; // full: site/device path
    DriverConfig driver;
    std::vector<TagConfig> tags;
};

struct PlantConfig {
    std::string site;
    MqttConfig mqtt;
    std::vector<DeviceConfig> devices;
};

[[nodiscard]] std::string_view driver_type(const DriverConfig& d) noexcept; // "modbus-tcp"

} // namespace f2c::config
