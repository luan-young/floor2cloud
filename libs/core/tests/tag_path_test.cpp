#include "f2c/core/tag_path.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <functional>
#include <string>
#include <string_view>
#include <vector>

using f2c::core::Errc;
using f2c::core::TagPath;

namespace {

TagPath must_parse(std::string_view text) {
    auto path = TagPath::parse(text);
    REQUIRE(path.has_value());
    return *path;
}

std::string repeat_segments(std::size_t count) {
    std::string text = "s0";
    for (std::size_t i = 1; i < count; ++i) {
        text += "/s" + std::to_string(i);
    }
    return text;
}

} // namespace

TEST_CASE("TagPath accepts the plan's example", "[core][tag_path]") {
    const auto path = must_parse("plant1/packaging/line1/cell_c/filler/speed");

    CHECK(path.str() == "plant1/packaging/line1/cell_c/filler/speed");
    CHECK(path.depth() == 6);
    CHECK(path.segments() == std::vector<std::string_view>{"plant1", "packaging", "line1", "cell_c",
                                                           "filler", "speed"});
}

TEST_CASE("TagPath accepts digits, dashes and the length limits", "[core][tag_path]") {
    CHECK(TagPath::parse("a").has_value());
    CHECK(TagPath::parse("0").has_value());
    CHECK(TagPath::parse("line-1/cell_2/x9").has_value());
    CHECK(TagPath::parse(std::string(64, 'a')).has_value());
    CHECK(TagPath::parse(repeat_segments(16)).has_value());
}

TEST_CASE("TagPath rejects malformed text", "[core][tag_path]") {
    const auto text =
        GENERATE(as<std::string>{}, "", "/", "a//b", "/a/b", "a/b/", "Filler", "plant1/Line1",
                 "a/+/b", "a/#", "a b", "a/ b", "_gateway", "a/_status", "-a", "a.b", "a/b\n",
                 std::string(65, 'a'), repeat_segments(17));
    CAPTURE(text);

    const auto path = TagPath::parse(text);

    REQUIRE_FALSE(path.has_value());
    CHECK(path.error().code == Errc::invalid_argument);
    CHECK_FALSE(path.error().message.empty());
}

TEST_CASE("is_valid_segment checks a single segment", "[core][tag_path]") {
    CHECK(f2c::core::is_valid_segment("filler"));
    CHECK(f2c::core::is_valid_segment("cell_c-2"));
    CHECK_FALSE(f2c::core::is_valid_segment(""));
    CHECK_FALSE(f2c::core::is_valid_segment("a/b"));
    CHECK_FALSE(f2c::core::is_valid_segment("_x"));
    CHECK_FALSE(f2c::core::is_valid_segment("X"));
    CHECK_FALSE(f2c::core::is_valid_segment(std::string(65, 'a')));
}

TEST_CASE("TagPath::append adds one segment and keeps the invariants", "[core][tag_path]") {
    const auto base = must_parse("plant1/packaging");

    SECTION("valid segment") {
        const auto path = base.append("line1");
        REQUIRE(path.has_value());
        CHECK(path->str() == "plant1/packaging/line1");
        CHECK(path->depth() == 3);
        CHECK(base.str() == "plant1/packaging");
    }

    SECTION("invalid segments") {
        const auto segment = GENERATE(as<std::string_view>{}, "", "Line1", "_x", "a/b", "+");
        CAPTURE(segment);
        const auto path = base.append(segment);
        REQUIRE_FALSE(path.has_value());
        CHECK(path.error().code == Errc::invalid_argument);
    }

    SECTION("too deep") {
        const auto full = must_parse(repeat_segments(16));
        const auto path = full.append("x");
        REQUIRE_FALSE(path.has_value());
        CHECK(path.error().code == Errc::invalid_argument);
    }
}

TEST_CASE("TagPath::join concatenates paths and keeps the invariants", "[core][tag_path]") {
    const auto prefix = must_parse("plant1/packaging/line1");

    SECTION("within the depth limit") {
        const auto path = prefix.join(must_parse("cell_c/filler/speed"));
        REQUIRE(path.has_value());
        CHECK(path->str() == "plant1/packaging/line1/cell_c/filler/speed");
        CHECK(path->depth() == 6);
    }

    SECTION("exactly at the depth limit") {
        const auto path = must_parse(repeat_segments(8)).join(must_parse(repeat_segments(8)));
        REQUIRE(path.has_value());
        CHECK(path->depth() == 16);
    }

    SECTION("too deep") {
        const auto path = must_parse(repeat_segments(8)).join(must_parse(repeat_segments(9)));
        REQUIRE_FALSE(path.has_value());
        CHECK(path.error().code == Errc::invalid_argument);
    }
}

TEST_CASE("TagPath compares and hashes by its text", "[core][tag_path]") {
    const auto a = must_parse("plant1/a");
    const auto a_again = must_parse("plant1/a");
    const auto b = must_parse("plant1/b");

    CHECK(a == a_again);
    CHECK(a != b);
    CHECK(a < b);
    CHECK(b > a);
    CHECK(must_parse("plant1") < must_parse("plant1/a"));

    const std::hash<TagPath> hash;
    CHECK(hash(a) == hash(a_again));
    CHECK(hash(a) == std::hash<std::string>{}(a.str()));
}
