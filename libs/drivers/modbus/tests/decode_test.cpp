#include "f2c/drivers/modbus/decode.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <tuple>
#include <variant>
#include <vector>

using Catch::Matchers::WithinRel;
using f2c::config::ByteOrder;
using f2c::config::LinearScaling;
using f2c::config::ModbusAddress;
using f2c::config::ModbusArea;
using f2c::config::ModbusType;
using f2c::config::WordOrder;
using f2c::core::Errc;
using f2c::core::Value;
using f2c::drivers::modbus::decode_bit;
using f2c::drivers::modbus::decode_registers;

namespace {

ModbusAddress address(ModbusType type, ByteOrder bytes = ByteOrder::big,
                      WordOrder words = WordOrder::big) {
    return ModbusAddress{.area = ModbusArea::holding,
                         .address = 0,
                         .type = type,
                         .byte_order = bytes,
                         .word_order = words};
}

std::uint16_t swap_bytes(std::uint16_t r) {
    return static_cast<std::uint16_t>((r << 8U) | (r >> 8U));
}

// The registers a device with the given orders holds for a big-endian 32-bit pattern (§ 5.2).
std::vector<std::uint16_t> encode32(std::uint32_t bits, ByteOrder bytes, WordOrder words) {
    auto high = static_cast<std::uint16_t>(bits >> 16U);
    auto low = static_cast<std::uint16_t>(bits & 0xFFFFU);
    if (bytes == ByteOrder::little) {
        high = swap_bytes(high);
        low = swap_bytes(low);
    }
    return words == WordOrder::big ? std::vector{high, low} : std::vector{low, high};
}

std::vector<std::uint16_t> encode16(std::uint16_t bits, ByteOrder bytes) {
    return {bytes == ByteOrder::little ? swap_bytes(bits) : bits};
}

std::uint32_t float_bits(float f) {
    static_assert(sizeof(float) == sizeof(std::uint32_t));
    std::uint32_t bits{};
    std::memcpy(&bits, &f, sizeof bits);
    return bits;
}

Value decode(ModbusType type, ByteOrder bytes, WordOrder words,
             const std::vector<std::uint16_t>& registers,
             const std::optional<LinearScaling>& scaling = std::nullopt) {
    auto value = decode_registers(address(type, bytes, words), registers, scaling);
    REQUIRE(value);
    return *value;
}

} // namespace

TEST_CASE("decode_registers applies the § 5.2 byte and word orders", "[modbus]") {
    // 0xAABBCCDD as stored by each order combination.
    const auto [bytes, words, r0, r1] = GENERATE(
        std::tuple{ByteOrder::big, WordOrder::big, std::uint16_t{0xAABB}, std::uint16_t{0xCCDD}},
        std::tuple{ByteOrder::big, WordOrder::little, std::uint16_t{0xCCDD}, std::uint16_t{0xAABB}},
        std::tuple{ByteOrder::little, WordOrder::big, std::uint16_t{0xBBAA}, std::uint16_t{0xDDCC}},
        std::tuple{ByteOrder::little, WordOrder::little, std::uint16_t{0xDDCC},
                   std::uint16_t{0xBBAA}});
    CAPTURE(bytes, words);
    const std::vector registers{r0, r1};

    CHECK(decode(ModbusType::uint32, bytes, words, registers) == Value{std::int64_t{0xAABBCCDD}});
}

TEST_CASE("decode_registers decodes every type in every order", "[modbus]") {
    const auto bytes = GENERATE(ByteOrder::big, ByteOrder::little);
    const auto words = GENERATE(WordOrder::big, WordOrder::little);
    CAPTURE(bytes, words);

    SECTION("int16") {
        CHECK(decode(ModbusType::int16, bytes, words, encode16(0xFFFF, bytes)) ==
              Value{std::int64_t{-1}});
        CHECK(decode(ModbusType::int16, bytes, words, encode16(0x8000, bytes)) ==
              Value{std::int64_t{-32768}});
        CHECK(decode(ModbusType::int16, bytes, words, encode16(0x0102, bytes)) ==
              Value{std::int64_t{258}});
    }
    SECTION("uint16") {
        CHECK(decode(ModbusType::uint16, bytes, words, encode16(0xFFFF, bytes)) ==
              Value{std::int64_t{65535}});
        CHECK(decode(ModbusType::uint16, bytes, words, encode16(0x0102, bytes)) ==
              Value{std::int64_t{258}});
    }
    SECTION("int32") {
        CHECK(decode(ModbusType::int32, bytes, words, encode32(0x80000000U, bytes, words)) ==
              Value{std::int64_t{std::numeric_limits<std::int32_t>::min()}});
        CHECK(decode(ModbusType::int32, bytes, words, encode32(0x7FFFFFFFU, bytes, words)) ==
              Value{std::int64_t{std::numeric_limits<std::int32_t>::max()}});
        CHECK(decode(ModbusType::int32, bytes, words, encode32(0xFFFFFFFFU, bytes, words)) ==
              Value{std::int64_t{-1}});
    }
    SECTION("uint32") {
        CHECK(decode(ModbusType::uint32, bytes, words, encode32(0xAABBCCDDU, bytes, words)) ==
              Value{std::int64_t{0xAABBCCDD}});
        CHECK(decode(ModbusType::uint32, bytes, words, encode32(0xFFFFFFFFU, bytes, words)) ==
              Value{std::int64_t{0xFFFFFFFF}});
    }
    SECTION("float32") {
        CHECK(decode(ModbusType::float32, bytes, words, encode32(float_bits(1.5F), bytes, words)) ==
              Value{1.5});
        const auto negative_zero =
            decode(ModbusType::float32, bytes, words, encode32(float_bits(-0.0F), bytes, words));
        REQUIRE(std::holds_alternative<double>(negative_zero));
        CHECK(std::get<double>(negative_zero) == 0.0);
        CHECK(std::signbit(std::get<double>(negative_zero)));
    }
}

TEST_CASE("decode_registers ignores registers past the tag's width", "[modbus]") {
    const std::vector<std::uint16_t> registers{0x0001, 0xFFFF, 0xFFFF};

    CHECK(decode(ModbusType::uint16, ByteOrder::big, WordOrder::big, registers) ==
          Value{std::int64_t{1}});
    CHECK(decode(ModbusType::uint32, ByteOrder::big, WordOrder::big, registers) ==
          Value{std::int64_t{0x1FFFF}});
}

TEST_CASE("decode_registers applies linear scaling", "[modbus]") {
    SECTION("scale") {
        const auto value = decode(ModbusType::int16, ByteOrder::big, WordOrder::big, {417},
                                  LinearScaling{.scale = 0.1, .offset = 0.0});
        REQUIRE(std::holds_alternative<double>(value));
        CHECK_THAT(std::get<double>(value), WithinRel(41.7, 1e-12));
    }
    SECTION("scale and offset on a negative value") {
        const auto value = decode(ModbusType::int16, ByteOrder::big, WordOrder::big, {0xFFF6},
                                  LinearScaling{.scale = 0.5, .offset = 100.0});
        REQUIRE(std::holds_alternative<double>(value));
        CHECK_THAT(std::get<double>(value), WithinRel(95.0, 1e-12));
    }
    SECTION("offset alone still yields a double") {
        CHECK(decode(ModbusType::uint16, ByteOrder::big, WordOrder::big, {7},
                     LinearScaling{.scale = 1.0, .offset = -2.0}) == Value{5.0});
    }
    SECTION("float32") {
        CHECK(decode(ModbusType::float32, ByteOrder::big, WordOrder::big,
                     encode32(float_bits(1.5F), ByteOrder::big, WordOrder::big),
                     LinearScaling{.scale = 2.0, .offset = 1.0}) == Value{4.0});
    }
}

TEST_CASE("decode_registers rejects non-finite values", "[modbus]") {
    const auto special =
        GENERATE(std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
                 -std::numeric_limits<float>::infinity());
    CAPTURE(special);
    const auto registers = encode32(float_bits(special), ByteOrder::big, WordOrder::big);

    const auto value = decode_registers(address(ModbusType::float32), registers, std::nullopt);

    REQUIRE_FALSE(value);
    CHECK(value.error().code == Errc::invalid_argument);
}

TEST_CASE("decode_registers rejects a scaled value that overflows to infinity", "[modbus]") {
    const auto registers =
        encode32(float_bits(std::numeric_limits<float>::max()), ByteOrder::big, WordOrder::big);

    const auto value = decode_registers(address(ModbusType::float32), registers,
                                        LinearScaling{.scale = 1e300, .offset = 0.0});

    REQUIRE_FALSE(value);
    CHECK(value.error().code == Errc::invalid_argument);
}

TEST_CASE("decode_registers rejects too few registers and bool", "[modbus]") {
    SECTION("too few registers") {
        const std::vector<std::uint16_t> one{0x0001};

        const auto value = decode_registers(address(ModbusType::uint32), one, std::nullopt);

        REQUIRE_FALSE(value);
        CHECK(value.error().code == Errc::invalid_argument);
    }
    SECTION("bool is read as bits, not registers") {
        const std::vector<std::uint16_t> one{0x0001};

        const auto value = decode_registers(address(ModbusType::boolean), one, std::nullopt);

        REQUIRE_FALSE(value);
        CHECK(value.error().code == Errc::invalid_argument);
    }
}

TEST_CASE("decode_bit yields a bool", "[modbus]") {
    CHECK(decode_bit(true) == Value{true});
    CHECK(decode_bit(false) == Value{false});
}
