#pragma once

#include "f2c/config/plant_config.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace f2c::drivers::modbus {

inline constexpr std::uint16_t max_registers_per_read = 125;
inline constexpr std::uint16_t max_bits_per_read = 2000;

// Registers a value of `type` spans (1 or 2); 1 for boolean.
[[nodiscard]] std::uint16_t width(config::ModbusType type) noexcept;

struct ReadBlock {
    config::ModbusArea area;
    std::uint16_t start;
    std::uint16_t count;           // registers or bits
    std::vector<std::size_t> tags; // indices into the device's tag list

    friend bool operator==(const ReadBlock&, const ReadBlock&) = default;
};

struct ScanGroup {
    std::chrono::milliseconds period;
    std::vector<ReadBlock> blocks;

    friend bool operator==(const ScanGroup&, const ScanGroup&) = default;
};

// Groups tags by scan period, then per area merges contiguous or overlapping address ranges
// into blocks no larger than the per-request maximum. Deterministic output: groups by
// ascending period, blocks by (area, start). Pure function.
[[nodiscard]] std::vector<ScanGroup> plan_scan_groups(std::span<const config::TagConfig> tags);

} // namespace f2c::drivers::modbus
