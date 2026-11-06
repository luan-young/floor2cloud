#include "f2c/messaging/topic.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <string_view>

using f2c::core::Errc;
using f2c::core::TagPath;
using namespace f2c::messaging;

namespace {

TagPath path(std::string_view text) {
    return TagPath::parse(text).value();
}

constexpr std::string_view example_path = "plant1/production/line1/cell-c/filler/temperature";
constexpr std::string_view example_topic =
    "f2c/v1/plant1/production/line1/cell-c/filler/temperature";

} // namespace

TEST_CASE("tag_topic prefixes the full tag path", "[messaging][topic]") {
    CHECK(tag_topic(path(example_path)) == example_topic);
}

TEST_CASE("tag_path_from_topic inverts tag_topic", "[messaging][topic]") {
    const auto text = GENERATE(as<std::string_view>{}, example_path, "plant1", "a/b-c/d_e/0");
    CAPTURE(text);

    const auto parsed = tag_path_from_topic(tag_topic(path(text)));

    REQUIRE(parsed.has_value());
    CHECK(*parsed == path(text));
}

TEST_CASE("tag_path_from_topic rejects topics outside the tag namespace", "[messaging][topic]") {
    const auto topic =
        GENERATE(as<std::string_view>{}, "", "f2c", "f2c/v1", "f2c/v1/",
                 "f2c/v2/plant1/filler/temperature", "x2c/v1/plant1/temperature",
                 "plant1/production/line1", "/f2c/v1/plant1", "f2c/v1/plant1/_gateway/gw/status",
                 "f2c/v1/Plant1/temperature", "f2c/v1/plant1//temperature");
    CAPTURE(topic);

    const auto parsed = tag_path_from_topic(topic);

    REQUIRE_FALSE(parsed.has_value());
    CHECK(parsed.error().code == Errc::invalid_argument);
}

TEST_CASE("gateway_status_topic follows the contract", "[messaging][topic]") {
    CHECK(gateway_status_topic("plant1", "gateway-1") == "f2c/v1/plant1/_gateway/gateway-1/status");
}

TEST_CASE("is_system_topic detects reserved segments", "[messaging][topic]") {
    CHECK(is_system_topic(gateway_status_topic("plant1", "gateway-1")));
    CHECK(is_system_topic("_sys"));
    CHECK(is_system_topic("f2c/v1/_x"));
    CHECK_FALSE(is_system_topic(example_topic));
    CHECK_FALSE(is_system_topic("f2c/v1/plant1/a_b"));
    CHECK_FALSE(is_system_topic(""));
}

TEST_CASE("historian_subscription covers every v1 topic", "[messaging][topic]") {
    CHECK(historian_subscription() == "f2c/v1/#");
}
