#pragma once

#include "f2c/config/plant_config.hpp"
#include "f2c/core/error.hpp"
#include "f2c/core/tag_sample.hpp"

#include <cstdint>
#include <optional>
#include <span>

namespace f2c::drivers::modbus {

// `words` begins at the tag's own address and holds at least width(type) registers.
// Unscaled integers → int64; float32 or any scaled value → double.
// Error (invalid_argument) for non-finite results; the caller turns it into bad quality.
[[nodiscard]] core::Expected<core::Value>
decode_registers(const config::ModbusAddress& address, std::span<const std::uint16_t> words,
                 const std::optional<config::LinearScaling>& scaling);

[[nodiscard]] core::Value decode_bit(bool bit) noexcept;

} // namespace f2c::drivers::modbus
