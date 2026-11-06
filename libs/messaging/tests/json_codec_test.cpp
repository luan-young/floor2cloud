#include "f2c/messaging/json_codec.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <variant>

using f2c::core::Errc;
using f2c::core::Quality;
using f2c::core::TagPath;
using f2c::core::TagSample;
using f2c::core::Timestamp;
using f2c::core::Value;
using namespace f2c::messaging;

namespace {

TagPath path() {
    return TagPath::parse("plant1/production/line1/cell-c/filler/temperature").value();
}

Timestamp at(std::string_view text) {
    return f2c::core::parse_rfc3339(text).value();
}

TagSample sample(Value value, Quality quality = Quality::good) {
    return TagSample{path(), std::move(value), quality, at("2026-10-20T13:05:01.120Z"),
                     at("2026-10-20T13:05:01.123Z")};
}

constexpr std::string_view times =
    R"("ts":"2026-10-20T13:05:01.120Z","gts":"2026-10-20T13:05:01.123Z")";

std::string payload(std::string_view value, std::string_view quality = "good") {
    return std::string{R"({"v":)"} + std::string{value} + R"(,"q":")" + std::string{quality} +
           R"(",)" + std::string{times} + "}";
}

} // namespace

TEST_CASE("encode_sample writes each value type", "[messaging][json]") {
    CHECK(encode_sample(sample(41.7)) == payload("41.7"));
    CHECK(encode_sample(sample(std::int64_t{18342})) == payload("18342"));
    CHECK(encode_sample(sample(std::int64_t{-5})) == payload("-5"));
    CHECK(encode_sample(sample(true)) == payload("true"));
    CHECK(encode_sample(sample(false)) == payload("false"));
    CHECK(encode_sample(sample(std::string{"BATCH-7 \"x\""})) == payload(R"("BATCH-7 \"x\"")"));
    CHECK(encode_sample(sample(std::monostate{}, Quality::bad)) == payload("null", "bad"));
    CHECK(encode_sample(sample(41.7, Quality::uncertain)) == payload("41.7", "uncertain"));
}

TEST_CASE("encode_sample keeps doubles distinguishable from integers", "[messaging][json]") {
    CHECK(encode_sample(sample(42.0)) == payload("42.0"));
    CHECK(encode_sample(sample(-0.0)) == payload("-0.0"));
    CHECK(encode_sample(sample(1e300)) == payload("1e+300"));
}

TEST_CASE("encode_sample writes non-finite doubles as null", "[messaging][json]") {
    const double value =
        GENERATE(std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(),
                 -std::numeric_limits<double>::infinity());

    CHECK(encode_sample(sample(value)) == payload("null"));
}

TEST_CASE("encode_sample truncates timestamps to milliseconds", "[messaging][json]") {
    auto s = sample(1.5);
    s.source_time = at("2026-10-20T13:05:01.120999999Z");

    CHECK(encode_sample(s) == payload("1.5"));
}

TEST_CASE("decode_sample reads each value type", "[messaging][json]") {
    const auto check = [](std::string_view value, const Value& expected) {
        CAPTURE(value);
        const auto decoded = decode_sample(path(), payload(value));
        REQUIRE(decoded.has_value());
        CHECK(decoded->value == expected);
    };

    check("41.7", Value{41.7});
    check("42.0", Value{42.0});
    check("1e3", Value{1000.0});
    check("18342", Value{std::int64_t{18342}});
    check("-5", Value{std::int64_t{-5}});
    check("0", Value{std::int64_t{0}});
    check("9223372036854775807", Value{std::numeric_limits<std::int64_t>::max()});
    check("-9223372036854775808", Value{std::numeric_limits<std::int64_t>::min()});
    check("true", Value{true});
    check("false", Value{false});
    check(R"("BATCH-7")", Value{std::string{"BATCH-7"}});
    check("null", Value{std::monostate{}});
}

TEST_CASE("decode_sample reads quality, times and keeps the given path", "[messaging][json]") {
    const auto decoded = decode_sample(path(), payload("1", "bad"));

    REQUIRE(decoded.has_value());
    CHECK(decoded->path == path());
    CHECK(decoded->quality == Quality::bad);
    CHECK(decoded->source_time == at("2026-10-20T13:05:01.120Z"));
    CHECK(decoded->gateway_time == at("2026-10-20T13:05:01.123Z"));
}

TEST_CASE("decode_sample accepts any key order, whitespace and RFC 3339 offsets",
          "[messaging][json]") {
    const auto decoded = decode_sample(path(), R"( { "gts" : "2026-10-20T15:05:01.123+02:00",
        "ts": "2026-10-20T13:05:01.12Z", "q": "good", "v": 2 } )");

    REQUIRE(decoded.has_value());
    CHECK(decoded->source_time == at("2026-10-20T13:05:01.120Z"));
    CHECK(decoded->gateway_time == at("2026-10-20T13:05:01.123Z"));
}

TEST_CASE("decode_sample ignores unknown keys", "[messaging][json]") {
    const auto decoded =
        decode_sample(path(), R"({"x":{"nested":[1,{"v":"no"}]},"v":3,"unit":"degC","q":"good",)" +
                                  std::string{times} + R"(,"y":[[],{}],"z":null})");

    REQUIRE(decoded.has_value());
    CHECK(decoded->value == Value{std::int64_t{3}});
}

TEST_CASE("encode_sample and decode_sample round-trip", "[messaging][json]") {
    const auto value =
        GENERATE(Value{std::monostate{}}, Value{true}, Value{std::int64_t{-7}},
                 Value{std::numeric_limits<std::int64_t>::max()}, Value{42.0}, Value{0.1},
                 Value{-1.5e-300}, Value{std::string{"é ünïcode ✓"}}, Value{std::string{}});
    const auto quality = GENERATE(Quality::good, Quality::uncertain, Quality::bad);
    const auto original = sample(value, quality);

    const auto decoded = decode_sample(path(), encode_sample(original));

    REQUIRE(decoded.has_value());
    CHECK(*decoded == original);
}

TEST_CASE("decode_sample rejects invalid payloads", "[messaging][json]") {
    const auto [text, code] = GENERATE(table<std::string, Errc>({
        // not JSON
        {"", Errc::parse_error},
        {"{", Errc::parse_error},
        {payload("1") + "x", Errc::parse_error},
        {payload("01"), Errc::parse_error},
        {payload("NaN"), Errc::parse_error},
        {"\x7b\"v\":\"\xff\"}", Errc::parse_error},
        // not an object
        {"[]", Errc::invalid_argument},
        {"null", Errc::invalid_argument},
        {"42", Errc::invalid_argument},
        {R"("text")", Errc::invalid_argument},
        // missing keys
        {"{}", Errc::invalid_argument},
        {R"({"q":"good",)" + std::string{times} + "}", Errc::invalid_argument},
        {R"({"v":1,)" + std::string{times} + "}", Errc::invalid_argument},
        {R"({"v":1,"q":"good","gts":"2026-10-20T13:05:01.123Z"})", Errc::invalid_argument},
        {R"({"v":1,"q":"good","ts":"2026-10-20T13:05:01.120Z"})", Errc::invalid_argument},
        // value out of range or of the wrong type
        {payload("9223372036854775808"), Errc::invalid_argument},
        {payload("-9223372036854775809"), Errc::invalid_argument},
        {payload("100000000000000000000000000000"), Errc::invalid_argument},
        {payload("[1]"), Errc::invalid_argument},
        {payload(R"({"x":1})"), Errc::invalid_argument},
        // quality
        {payload("1", "ok"), Errc::invalid_argument},
        {payload("1", "Good"), Errc::invalid_argument},
        {R"({"v":1,"q":1,)" + std::string{times} + "}", Errc::invalid_argument},
        {R"({"v":1,"q":null,)" + std::string{times} + "}", Errc::invalid_argument},
        // timestamps
        {R"({"v":1,"q":"good","ts":"2026-10-20 13:05:01Z","gts":"2026-10-20T13:05:01.123Z"})",
         Errc::invalid_argument},
        {R"({"v":1,"q":"good","ts":"2026-10-20T13:05:01.120Z","gts":"yesterday"})",
         Errc::invalid_argument},
        {R"({"v":1,"q":"good","ts":1761000000,"gts":"2026-10-20T13:05:01.123Z"})",
         Errc::invalid_argument},
    }));
    CAPTURE(text);

    const auto decoded = decode_sample(path(), text);

    REQUIRE_FALSE(decoded.has_value());
    CHECK(decoded.error().code == code);
    CHECK_FALSE(decoded.error().message.empty());
}

TEST_CASE("decode_sample explains why a payload is rejected", "[messaging][json]") {
    const auto [text, fragment] = GENERATE(table<std::string_view, std::string_view>({
        {R"("text")", "not an object"},
        {"[1]", "not an object"},
        {R"({"q":"good"})", "'v'"},
        {R"({"v":[1]})", "'v'"},
        {R"({"v":1,"q":"ok"})", "'q'"},
        {R"({"v":1,"q":"good","ts":"x"})", "'ts'"},
        {R"({"v":1,"q":"good","ts":"2026-10-20T13:05:01Z"})", "'gts'"},
        {R"({"v":18446744073709551616,"q":"good"})", "int64"},
    }));
    CAPTURE(text);

    const auto decoded = decode_sample(path(), text);

    REQUIRE_FALSE(decoded.has_value());
    CHECK(decoded.error().message.find(fragment) != std::string::npos);
}

TEST_CASE("decode_sample accepts doubles beyond the int64 range", "[messaging][json]") {
    const auto decoded = decode_sample(path(), payload("1e30"));

    REQUIRE(decoded.has_value());
    CHECK(decoded->value == Value{1e30});
}

TEST_CASE("encode_status writes state, time and version", "[messaging][json]") {
    const std::string version = F2C_VERSION;

    CHECK(encode_status(GatewayState::online, at("2026-10-20T13:05:00Z")) ==
          R"({"state":"online","ts":"2026-10-20T13:05:00.000Z","version":")" + version + R"("})");
    CHECK(encode_status(GatewayState::offline, at("2026-10-20T14:00:00Z")) ==
          R"({"state":"offline","ts":"2026-10-20T14:00:00.000Z","version":")" + version + R"("})");
}

TEST_CASE("encode_status omits ts for the last will", "[messaging][json]") {
    CHECK(encode_status(GatewayState::offline, std::nullopt) ==
          std::string{R"({"state":"offline","version":")"} + F2C_VERSION + R"("})");
}
