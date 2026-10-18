#include "f2c/core/timestamp.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <chrono>
#include <string_view>

using f2c::core::Errc;
using f2c::core::format_rfc3339_ms;
using f2c::core::parse_rfc3339;
using f2c::core::Timestamp;
using namespace std::chrono;
using namespace std::chrono_literals;

namespace {

// 2026-10-20T13:05:01 UTC, the example used throughout the spec.
constexpr Timestamp example_second{sys_days{2026y / October / 20} + 13h + 5min + 1s};

} // namespace

TEST_CASE("format_rfc3339_ms writes milliseconds and a Z suffix", "[core][timestamp]") {
    CHECK(format_rfc3339_ms(example_second + 120ms) == "2026-10-20T13:05:01.120Z");
    CHECK(format_rfc3339_ms(example_second) == "2026-10-20T13:05:01.000Z");
}

TEST_CASE("format_rfc3339_ms truncates instead of rounding", "[core][timestamp]") {
    CHECK(format_rfc3339_ms(example_second + 120'999'900ns) == "2026-10-20T13:05:01.120Z");
    CHECK(format_rfc3339_ms(example_second + 999'999'999ns) == "2026-10-20T13:05:01.999Z");
}

TEST_CASE("format_rfc3339_ms of the epoch and before it", "[core][timestamp]") {
    CHECK(format_rfc3339_ms(Timestamp{}) == "1970-01-01T00:00:00.000Z");
    CHECK(format_rfc3339_ms(Timestamp{} - 1ns) == "1969-12-31T23:59:59.999Z");
}

TEST_CASE("parse_rfc3339 accepts UTC and offsets", "[core][timestamp]") {
    SECTION("Z suffix, no fraction") {
        const auto t = parse_rfc3339("2026-10-20T13:05:01Z");
        REQUIRE(t.has_value());
        CHECK(*t == example_second);
    }

    SECTION("positive offset") {
        const auto t = parse_rfc3339("2026-10-20T15:05:01.120+02:00");
        REQUIRE(t.has_value());
        CHECK(*t == example_second + 120ms);
    }

    SECTION("negative offset crossing midnight") {
        const auto t = parse_rfc3339("2026-10-19T23:35:01-13:30");
        REQUIRE(t.has_value());
        CHECK(*t == example_second);
    }

    SECTION("nine fractional digits keep nanoseconds") {
        const auto t = parse_rfc3339("2026-10-20T13:05:01.123456789Z");
        REQUIRE(t.has_value());
        CHECK(*t == example_second + 123'456'789ns);
    }

    SECTION("one fractional digit") {
        const auto t = parse_rfc3339("2026-10-20T13:05:01.5Z");
        REQUIRE(t.has_value());
        CHECK(*t == example_second + 500ms);
    }

    SECTION("lowercase separators, as RFC 3339 allows") {
        const auto t = parse_rfc3339("2026-10-20t13:05:01z");
        REQUIRE(t.has_value());
        CHECK(*t == example_second);
    }

    SECTION("leap day") {
        const auto t = parse_rfc3339("2024-02-29T00:00:00Z");
        REQUIRE(t.has_value());
        CHECK(*t == Timestamp{sys_days{2024y / February / 29}});
    }
}

TEST_CASE("format and parse round-trip at millisecond precision", "[core][timestamp]") {
    const auto t = GENERATE(Timestamp{}, example_second + 120ms, example_second + 999ms,
                            Timestamp{sys_days{2000y / January / 1}} + 1ms);
    CAPTURE(format_rfc3339_ms(t));

    const auto parsed = parse_rfc3339(format_rfc3339_ms(t));

    REQUIRE(parsed.has_value());
    CHECK(*parsed == t);
}

TEST_CASE("parse_rfc3339 rejects malformed text", "[core][timestamp]") {
    const auto text =
        GENERATE(as<std::string_view>{}, "", "garbage", "2026-10-20 13:05:01Z", "2026-10-20",
                 "2026-10-20T13:05:01", "2026-13-20T13:05:01Z", "2026-00-20T13:05:01Z",
                 "2026-02-30T13:05:01Z", "2025-02-29T00:00:00Z", "2026-10-20T24:00:00Z",
                 "2026-10-20T13:60:01Z", "2026-10-20T13:05:60Z", "2026-10-20T13:05:01.Z",
                 "2026-10-20T13:05:01.1234567890Z", "2026-10-20T13:05:01+0200",
                 "2026-10-20T13:05:01+24:00", "2026-10-20T13:05:01+02:60", "2026-1-20T13:05:01Z",
                 "2026-10-20T13:05:01Zjunk", "+026-10-20T13:05:01Z");
    CAPTURE(text);

    const auto t = parse_rfc3339(text);

    REQUIRE_FALSE(t.has_value());
    CHECK(t.error().code == Errc::parse_error);
    CHECK_FALSE(t.error().message.empty());
}
