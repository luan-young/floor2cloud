#include "f2c/drivers/modbus/decode.hpp"

#include "f2c/drivers/modbus/read_plan.hpp"

#include <bit>
#include <cmath>
#include <string>
#include <variant>

namespace f2c::drivers::modbus {

namespace {

using config::ByteOrder;
using config::ModbusType;
using config::WordOrder;

// Registers arrive with the first byte on the wire as the high byte (§ 5.2 "big").
std::uint16_t to_big_endian(std::uint16_t reg, ByteOrder order) noexcept {
    return order == ByteOrder::little ? static_cast<std::uint16_t>((reg << 8U) | (reg >> 8U)) : reg;
}

core::Value raw_value(ModbusType type, std::uint16_t first, std::uint32_t both) noexcept {
    switch (type) {
    case ModbusType::int16:
        return std::int64_t{static_cast<std::int16_t>(first)};
    case ModbusType::uint16:
        return std::int64_t{first};
    case ModbusType::int32:
        return std::int64_t{static_cast<std::int32_t>(both)};
    case ModbusType::uint32:
        return std::int64_t{both};
    case ModbusType::float32:
        return double{std::bit_cast<float>(both)};
    case ModbusType::boolean:
        break;
    }
    return {};
}

} // namespace

core::Expected<core::Value> decode_registers(const config::ModbusAddress& address,
                                             std::span<const std::uint16_t> words,
                                             const std::optional<config::LinearScaling>& scaling) {
    if (address.type == ModbusType::boolean) {
        return core::make_error(core::Errc::invalid_argument,
                                "bool is read from coils or discrete inputs, not registers");
    }
    const auto needed = width(address.type);
    if (words.size() < needed) {
        return core::make_error(core::Errc::invalid_argument, "need " + std::to_string(needed) +
                                                                  " registers, got " +
                                                                  std::to_string(words.size()));
    }

    const auto first = to_big_endian(words[0], address.byte_order);
    std::uint32_t both = 0;
    if (needed == 2) {
        const auto second = to_big_endian(words[1], address.byte_order);
        const auto [high, low] = address.word_order == WordOrder::big ? std::pair{first, second}
                                                                      : std::pair{second, first};
        both = (std::uint32_t{high} << 16U) | low;
    }
    auto value = raw_value(address.type, first, both);

    if (scaling) {
        const double raw = std::holds_alternative<std::int64_t>(value)
                               ? static_cast<double>(std::get<std::int64_t>(value))
                               : std::get<double>(value);
        value = raw * scaling->scale + scaling->offset;
    }
    if (const auto* real = std::get_if<double>(&value); real != nullptr && !std::isfinite(*real)) {
        return core::make_error(core::Errc::invalid_argument, "value is not finite");
    }
    return value;
}

core::Value decode_bit(bool bit) noexcept {
    return bit;
}

} // namespace f2c::drivers::modbus
