#include "f2c/core/error.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

using f2c::core::Errc;
using f2c::core::Expected;

TEST_CASE("Errc to_string names each code", "[core][error]") {
    CHECK(f2c::core::to_string(Errc::invalid_argument) == "invalid_argument");
    CHECK(f2c::core::to_string(Errc::parse_error) == "parse_error");
    CHECK(f2c::core::to_string(Errc::not_found) == "not_found");
    CHECK(f2c::core::to_string(Errc::io_error) == "io_error");
    CHECK(f2c::core::to_string(Errc::timeout) == "timeout");
    CHECK(f2c::core::to_string(Errc::protocol_error) == "protocol_error");
    CHECK(f2c::core::to_string(Errc::unavailable) == "unavailable");
}

TEST_CASE("make_error converts into a failed Expected", "[core][error]") {
    const auto fail = []() -> Expected<int> {
        return f2c::core::make_error(Errc::timeout, "filler: read holding 0..4: timeout");
    };

    const auto result = fail();

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == Errc::timeout);
    CHECK(result.error().message == "filler: read holding 0..4: timeout");
}
