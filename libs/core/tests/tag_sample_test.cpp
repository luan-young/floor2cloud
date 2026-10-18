#include "f2c/core/tag_sample.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <chrono>
#include <string_view>
#include <variant>

using f2c::core::Errc;
using f2c::core::parse_quality;
using f2c::core::Quality;
using f2c::core::TagPath;
using f2c::core::TagSample;
using f2c::core::Timestamp;

TEST_CASE("Quality to_string and parse_quality round-trip", "[core][quality]") {
    const auto quality = GENERATE(Quality::good, Quality::uncertain, Quality::bad);

    const auto parsed = parse_quality(f2c::core::to_string(quality));

    REQUIRE(parsed.has_value());
    CHECK(*parsed == quality);
}

TEST_CASE("Quality strings match the MQTT contract", "[core][quality]") {
    CHECK(f2c::core::to_string(Quality::good) == "good");
    CHECK(f2c::core::to_string(Quality::uncertain) == "uncertain");
    CHECK(f2c::core::to_string(Quality::bad) == "bad");
}

TEST_CASE("parse_quality rejects unknown strings", "[core][quality]") {
    const auto text = GENERATE(as<std::string_view>{}, "", "Good", "GOOD", "ok", "good ");
    CAPTURE(text);

    const auto parsed = parse_quality(text);

    REQUIRE_FALSE(parsed.has_value());
    CHECK(parsed.error().code == Errc::invalid_argument);
}

TEST_CASE("TagSample defaults to no value and compares member-wise", "[core][tag_sample]") {
    const auto path = TagPath::parse("plant1/filler/speed");
    REQUIRE(path.has_value());
    const Timestamp t{std::chrono::seconds{1}};

    const TagSample never_read{*path, {}, Quality::bad, t, t};
    CHECK(std::holds_alternative<std::monostate>(never_read.value));

    const TagSample good{*path, 120.0, Quality::good, t, t};
    CHECK(good == TagSample{*path, 120.0, Quality::good, t, t});
    CHECK(good != never_read);
    CHECK(good != TagSample{*path, std::int64_t{120}, Quality::good, t, t});
}
