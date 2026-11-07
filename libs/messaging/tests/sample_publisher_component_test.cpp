#include "f2c/messaging/json_codec.hpp"
#include "f2c/messaging/sample_publisher.hpp"
#include "f2c/messaging/testing/fake_mqtt_client.hpp"

#include <asio/executor_work_guard.hpp>
#include <asio/io_context.hpp>
#include <asio/post.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <functional>
#include <future>
#include <thread>
#include <vector>

using f2c::core::Quality;
using f2c::core::TagPath;
using f2c::core::TagSample;
using f2c::messaging::PublisherOptions;
using f2c::messaging::SamplePublisher;
using f2c::messaging::testing::FakeMqttClient;
using namespace std::chrono_literals;

namespace {

bool wait_until(const std::function<bool()>& predicate, std::chrono::milliseconds timeout) {
    const auto end = std::chrono::steady_clock::now() + timeout;
    while (!predicate()) {
        if (std::chrono::steady_clock::now() > end) {
            return false;
        }
        std::this_thread::sleep_for(5ms);
    }
    return true;
}

// The publisher on its own io_context thread, as in the gateway (ADR-0010).
struct PublisherThread {
    explicit PublisherThread(PublisherOptions options)
        : publisher{io.get_executor(), client, options} {
        asio::post(io, [this] { publisher.start(); });
        thread = std::thread{[this] { io.run(); }};
    }

    PublisherThread(const PublisherThread&) = delete;
    PublisherThread& operator=(const PublisherThread&) = delete;
    PublisherThread(PublisherThread&&) = delete;
    PublisherThread& operator=(PublisherThread&&) = delete;

    ~PublisherThread() {
        asio::post(io, [this] { publisher.stop(); });
        guard.reset();
        thread.join();
    }

    asio::io_context io;
    asio::executor_work_guard<asio::io_context::executor_type> guard{io.get_executor()};
    FakeMqttClient client;
    SamplePublisher publisher;
    std::thread thread;
};

TagSample temperature() {
    const auto now = std::chrono::system_clock::now();
    return TagSample{TagPath::parse("plant1/filler/temperature").value(), 41.7, Quality::good, now,
                     now};
}

f2c::core::TagSample decode(const FakeMqttClient::Published& published) {
    return f2c::messaging::decode_sample(TagPath::parse("plant1/filler/temperature").value(),
                                         published.payload)
        .value();
}

} // namespace

TEST_CASE("SamplePublisher republishes an unchanged tag on the heartbeat",
          "[messaging][publisher][component]") {
    PublisherThread p{PublisherOptions{1, 1s}};
    const std::vector samples{temperature()};

    p.publisher.submit(samples);
    REQUIRE(wait_until([&] { return p.client.published().size() == 1; }, 1s));
    REQUIRE(wait_until([&] { return p.client.published().size() == 2; }, 3s));

    const auto published = p.client.published();
    const auto first = decode(published[0]);
    const auto heartbeat = decode(published[1]);
    CHECK(heartbeat.value == first.value);
    CHECK(heartbeat.source_time == first.source_time);
    CHECK(heartbeat.gateway_time - first.gateway_time >= 1s); // a fresh gts
    CHECK(published[1].topic == published[0].topic);
}

TEST_CASE("SamplePublisher sends heartbeats regularly", "[messaging][publisher][component]") {
    PublisherThread p{PublisherOptions{1, 1s}};
    const std::vector samples{temperature()};
    const auto start = std::chrono::steady_clock::now();

    p.publisher.submit(samples);

    REQUIRE(wait_until([&] { return p.client.published().size() == 3; }, 4s));
    const auto elapsed = std::chrono::steady_clock::now() - start;
    CHECK(elapsed >= 2s);
    CHECK(elapsed < 3500ms);
}
