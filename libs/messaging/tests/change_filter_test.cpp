#include "f2c/messaging/change_filter.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <string_view>
#include <variant>

using f2c::core::Quality;
using f2c::core::TagPath;
using f2c::core::TagSample;
using f2c::core::Timestamp;
using f2c::core::Value;
using f2c::messaging::ChangeFilter;
using namespace std::chrono_literals;

namespace {

constexpr ChangeFilter::TimePoint t0{};

TagSample sample(std::string_view tag, Value value, Quality quality = Quality::good,
                 std::chrono::milliseconds source_offset = 0ms) {
    const Timestamp ts = Timestamp{} + source_offset;
    return TagSample{TagPath::parse(tag).value(), std::move(value), quality, ts, ts};
}

TagSample temp(double value, Quality quality = Quality::good,
               std::chrono::milliseconds offset = 0ms) {
    return sample("plant1/filler/temperature", value, quality, offset);
}

} // namespace

TEST_CASE("ChangeFilter publishes the first sample of a tag", "[messaging][change_filter]") {
    ChangeFilter filter{10s};

    CHECK(filter.offer(temp(41.7), t0));
}

TEST_CASE("ChangeFilter suppresses unchanged value and quality", "[messaging][change_filter]") {
    ChangeFilter filter{10s};
    REQUIRE(filter.offer(temp(41.7), t0));

    CHECK_FALSE(filter.offer(temp(41.7, Quality::good, 1000ms), t0 + 1s));
    CHECK_FALSE(filter.offer(temp(41.7, Quality::good, 2000ms), t0 + 2s));
}

TEST_CASE("ChangeFilter publishes value changes", "[messaging][change_filter]") {
    ChangeFilter filter{10s};
    REQUIRE(filter.offer(temp(41.7), t0));

    CHECK(filter.offer(temp(41.8), t0 + 1s));
    CHECK_FALSE(filter.offer(temp(41.8), t0 + 2s));
    CHECK(filter.offer(temp(41.7), t0 + 3s));
}

TEST_CASE("ChangeFilter compares values exactly, including their type",
          "[messaging][change_filter]") {
    ChangeFilter filter{10s};
    constexpr std::string_view tag = "plant1/filler/count";
    REQUIRE(filter.offer(sample(tag, std::int64_t{1}), t0));

    CHECK(filter.offer(sample(tag, 1.0), t0 + 1s));
    CHECK(filter.offer(sample(tag, true), t0 + 2s));
    CHECK(filter.offer(sample(tag, std::monostate{}), t0 + 3s));
    CHECK_FALSE(filter.offer(sample(tag, std::monostate{}), t0 + 4s));
}

TEST_CASE("ChangeFilter publishes quality changes with the same value",
          "[messaging][change_filter]") {
    ChangeFilter filter{10s};
    REQUIRE(filter.offer(temp(41.7), t0));

    CHECK(filter.offer(temp(41.7, Quality::bad), t0 + 1s));
    CHECK_FALSE(filter.offer(temp(41.7, Quality::bad), t0 + 2s));
    CHECK(filter.offer(temp(41.7, Quality::good), t0 + 3s));
}

TEST_CASE("ChangeFilter heartbeat returns the latest sample after the interval",
          "[messaging][change_filter]") {
    ChangeFilter filter{10s};
    REQUIRE(filter.offer(temp(41.7, Quality::good, 0ms), t0));
    REQUIRE_FALSE(filter.offer(temp(41.7, Quality::good, 5000ms), t0 + 5s));

    CHECK(filter.take_heartbeats(t0 + 9999ms).empty());

    const auto heartbeats = filter.take_heartbeats(t0 + 10s);

    REQUIRE(heartbeats.size() == 1);
    CHECK(heartbeats[0] == temp(41.7, Quality::good, 5000ms)); // latest, not last published
    CHECK(filter.take_heartbeats(t0 + 10s).empty());           // marked published
    CHECK(filter.take_heartbeats(t0 + 19s).empty());
    CHECK(filter.take_heartbeats(t0 + 20s).size() == 1);
}

TEST_CASE("ChangeFilter heartbeat restarts on every publish", "[messaging][change_filter]") {
    ChangeFilter filter{10s};
    REQUIRE(filter.offer(temp(1.0), t0));
    REQUIRE(filter.offer(temp(2.0), t0 + 6s));

    CHECK(filter.take_heartbeats(t0 + 10s).empty());
    CHECK(filter.take_heartbeats(t0 + 16s).size() == 1);
}

TEST_CASE("ChangeFilter compares against the sample sent by a heartbeat",
          "[messaging][change_filter]") {
    ChangeFilter filter{10s};
    REQUIRE(filter.offer(temp(1.0), t0));
    REQUIRE(filter.take_heartbeats(t0 + 10s).size() == 1);

    CHECK_FALSE(filter.offer(temp(1.0), t0 + 11s));
    CHECK(filter.offer(temp(2.0), t0 + 12s));
}

TEST_CASE("ChangeFilter with heartbeat 0 never sends heartbeats", "[messaging][change_filter]") {
    ChangeFilter filter{0s};
    REQUIRE(filter.offer(temp(41.7), t0));

    CHECK(filter.take_heartbeats(t0).empty());
    CHECK(filter.take_heartbeats(t0 + 24h).empty());
    CHECK_FALSE(filter.next_heartbeat().has_value());
    CHECK_FALSE(filter.offer(temp(41.7), t0 + 24h));
}

TEST_CASE("ChangeFilter next_heartbeat is the earliest due time", "[messaging][change_filter]") {
    ChangeFilter filter{10s};

    CHECK_FALSE(filter.next_heartbeat().has_value());

    REQUIRE(filter.offer(sample("plant1/a", 1.0), t0 + 3s));
    CHECK(filter.next_heartbeat() == t0 + 13s);

    REQUIRE(filter.offer(sample("plant1/b", 1.0), t0 + 5s));
    CHECK(filter.next_heartbeat() == t0 + 13s);

    REQUIRE(filter.offer(sample("plant1/a", 2.0), t0 + 7s));
    CHECK(filter.next_heartbeat() == t0 + 15s);

    REQUIRE(filter.take_heartbeats(t0 + 15s).size() == 1);
    CHECK(filter.next_heartbeat() == t0 + 17s);
}

TEST_CASE("ChangeFilter tracks tags independently", "[messaging][change_filter]") {
    ChangeFilter filter{10s};
    REQUIRE(filter.offer(sample("plant1/a", 1.0), t0));

    CHECK(filter.offer(sample("plant1/b", 1.0), t0 + 1s));
    CHECK_FALSE(filter.offer(sample("plant1/a", 1.0), t0 + 2s));
    CHECK_FALSE(filter.offer(sample("plant1/b", 1.0), t0 + 2s));
    CHECK(filter.offer(sample("plant1/b", 2.0), t0 + 3s));

    const auto first = filter.take_heartbeats(t0 + 10s);
    REQUIRE(first.size() == 1);
    CHECK(first[0].path.str() == "plant1/a");

    REQUIRE(filter.offer(sample("plant1/c", 1.0), t0 + 4s));
    const auto later = filter.take_heartbeats(t0 + 30s);
    REQUIRE(later.size() == 3);
    CHECK(later[0].path.str() == "plant1/a"); // ordered by path
    CHECK(later[1].path.str() == "plant1/b");
    CHECK(later[2].path.str() == "plant1/c");
}
