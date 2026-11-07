#include "f2c/core/testing/manual_clock.hpp"
#include "f2c/messaging/json_codec.hpp"
#include "f2c/messaging/sample_publisher.hpp"
#include "f2c/messaging/testing/fake_mqtt_client.hpp"
#include "f2c/messaging/topic.hpp"

#include <asio/io_context.hpp>
#include <asio/post.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <string_view>
#include <thread>
#include <vector>

using f2c::core::Errc;
using f2c::core::Error;
using f2c::core::Quality;
using f2c::core::TagPath;
using f2c::core::TagSample;
using f2c::core::Timestamp;
using f2c::core::Value;
using f2c::core::testing::ManualClock;
using f2c::messaging::encode_sample;
using f2c::messaging::PublisherOptions;
using f2c::messaging::SamplePublisher;
using f2c::messaging::tag_topic;
using f2c::messaging::testing::FakeMqttClient;
using namespace std::chrono_literals;

namespace {

using std::chrono::sys_days;
constexpr Timestamp source_time =
    sys_days{std::chrono::October / 20 / 2026} + 13h + 5min + 1s + 120ms;
constexpr Timestamp start_time = sys_days{std::chrono::October / 20 / 2026} + 13h + 5min + 2s;

TagSample sample(std::string_view tag, Value value, Quality quality = Quality::good) {
    // Drivers copy source_time into gateway_time; the publisher overwrites it.
    return TagSample{TagPath::parse(tag).value(), std::move(value), quality, source_time,
                     source_time};
}

TagSample stamped(TagSample s, Timestamp gateway_time) {
    s.gateway_time = gateway_time;
    return s;
}

FakeMqttClient::Published expected(const TagSample& s, int qos) {
    return FakeMqttClient::Published{tag_topic(s.path), encode_sample(s), qos, false};
}

// Runs the handlers ready now. restart() first: an io_context that ran out of work stays stopped.
void run_ready(asio::io_context& io) {
    io.restart();
    io.poll();
}

struct Fixture {
    asio::io_context io;
    FakeMqttClient client;
    ManualClock clock{start_time};
    SamplePublisher publisher{io.get_executor(), client, PublisherOptions{1, 30s}, clock};
};

} // namespace

TEST_CASE("SamplePublisher publishes on the executor, not on the submitting thread",
          "[messaging][publisher]") {
    Fixture f;
    const std::vector samples{sample("plant1/filler/temperature", 41.7)};

    std::thread driver{[&] { f.publisher.submit(samples); }};
    driver.join();

    CHECK(f.client.published().empty());

    run_ready(f.io);

    CHECK(f.client.published().size() == 1);
}

TEST_CASE("SamplePublisher stamps gateway_time and publishes topic, QoS and payload",
          "[messaging][publisher]") {
    asio::io_context io;
    FakeMqttClient client;
    const ManualClock clock{start_time};
    SamplePublisher publisher{io.get_executor(), client, PublisherOptions{0, 30s}, clock};
    const auto temperature = sample("plant1/filler/temperature", 41.7);
    const auto count = sample("plant1/filler/count", std::int64_t{18342});
    const std::vector samples{temperature, count};

    publisher.submit(samples);
    run_ready(io);

    const auto published = client.published();
    REQUIRE(published.size() == 2);
    CHECK(published[0] == expected(stamped(temperature, start_time), 0));
    CHECK(published[1] == expected(stamped(count, start_time), 0));
}

TEST_CASE("SamplePublisher stamps each batch with the time it is handled",
          "[messaging][publisher]") {
    Fixture f;
    const std::vector first{sample("plant1/filler/temperature", 41.7)};
    const std::vector second{sample("plant1/filler/temperature", 41.8)};

    f.publisher.submit(first);
    run_ready(f.io);
    f.clock.advance(1500ms);
    f.publisher.submit(second);
    run_ready(f.io);

    const auto published = f.client.published();
    REQUIRE(published.size() == 2);
    CHECK(published[1] == expected(stamped(second[0], start_time + 1500ms), 1));
}

TEST_CASE("SamplePublisher copies the samples before returning", "[messaging][publisher]") {
    Fixture f;
    std::vector samples{sample("plant1/filler/temperature", 41.7)};

    f.publisher.submit(samples);
    samples[0].value = 99.0;
    run_ready(f.io);

    const auto published = f.client.published();
    REQUIRE(published.size() == 1);
    CHECK(published[0].payload.find("41.7") != std::string::npos);
}

TEST_CASE("SamplePublisher does not publish unchanged samples", "[messaging][publisher]") {
    Fixture f;
    const auto submit = [&](Value value, Quality quality = Quality::good) {
        const std::vector samples{sample("plant1/filler/temperature", std::move(value), quality)};
        f.publisher.submit(samples);
        f.clock.advance(1s);
        run_ready(f.io);
    };

    submit(41.7);
    submit(41.7);
    submit(41.7);
    submit(41.8);
    submit(41.8, Quality::bad);
    submit(41.8, Quality::bad);

    const auto published = f.client.published();
    REQUIRE(published.size() == 3);
    CHECK(published[0].payload.find(R"("v":41.7,"q":"good")") != std::string::npos);
    CHECK(published[1].payload.find(R"("v":41.8,"q":"good")") != std::string::npos);
    CHECK(published[2].payload.find(R"("v":41.8,"q":"bad")") != std::string::npos);
}

TEST_CASE("SamplePublisher keeps submission order", "[messaging][publisher]") {
    Fixture f;
    for (int i = 0; i < 50; ++i) {
        const std::vector samples{sample("plant1/filler/count", std::int64_t{i})};
        f.publisher.submit(samples);
    }

    run_ready(f.io);

    const auto published = f.client.published();
    REQUIRE(published.size() == 50);
    for (std::size_t i = 0; i < published.size(); ++i) {
        CAPTURE(i);
        CHECK(published[i].payload.starts_with(R"({"v":)" + std::to_string(i) + ","));
    }
}

TEST_CASE("SamplePublisher keeps going after a failed publish", "[messaging][publisher]") {
    Fixture f;
    f.client.fail_publish(Error{Errc::invalid_argument, "refused"});
    const std::vector first{sample("plant1/filler/temperature", 41.7)};
    f.publisher.submit(first);
    run_ready(f.io);
    REQUIRE(f.client.published().empty());

    f.client.fail_publish(std::nullopt);
    const std::vector second{sample("plant1/filler/temperature", 41.8)};
    f.publisher.submit(second);
    run_ready(f.io);

    CHECK(f.client.published().size() == 1);
}

TEST_CASE("SamplePublisher leaves no pending work after stop", "[messaging][publisher]") {
    Fixture f;
    f.publisher.start();
    const std::vector samples{sample("plant1/filler/temperature", 41.7)};
    f.publisher.submit(samples);
    run_ready(f.io); // arms the 30 s heartbeat timer

    asio::post(f.io, [&] { f.publisher.stop(); });
    f.io.run_for(2s); // returns at once if nothing is pending

    CHECK(f.io.stopped());
}

TEST_CASE("SamplePublisher arms no heartbeat timer when heartbeats are disabled",
          "[messaging][publisher]") {
    asio::io_context io;
    FakeMqttClient client;
    const ManualClock clock{start_time};
    SamplePublisher publisher{io.get_executor(), client, PublisherOptions{1, 0s}, clock};
    publisher.start();
    const std::vector samples{sample("plant1/filler/temperature", 41.7)};

    publisher.submit(samples);
    io.run_for(2s);

    CHECK(io.stopped());
    CHECK(client.published().size() == 1);
}
