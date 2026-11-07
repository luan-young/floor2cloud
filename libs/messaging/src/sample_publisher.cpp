#include "f2c/messaging/sample_publisher.hpp"

#include "f2c/messaging/json_codec.hpp"
#include "f2c/messaging/topic.hpp"

#include <asio/error.hpp>
#include <asio/post.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <utility>

namespace f2c::messaging {

SamplePublisher::SamplePublisher(const asio::any_io_executor& executor, MqttClient& client,
                                 PublisherOptions options, const core::Clock& clock)
    : executor_{executor}, client_{&client}, options_{options}, clock_{&clock},
      filter_{options.heartbeat}, timer_{executor} {}

void SamplePublisher::submit(std::span<const core::TagSample> samples) {
    asio::post(executor_, [this, copy = std::vector(samples.begin(), samples.end())]() mutable {
        handle(std::move(copy));
    });
}

void SamplePublisher::start() {
    running_ = true;
    arm_heartbeat();
}

void SamplePublisher::stop() {
    running_ = false;
    timer_.cancel();
}

void SamplePublisher::handle(std::vector<core::TagSample> samples) {
    const auto gateway_time = clock_->now();
    const auto now = clock_->steady_now();
    for (auto& sample : samples) {
        sample.gateway_time = gateway_time;
        if (filter_.offer(sample, now)) {
            publish(sample);
        }
    }
    arm_heartbeat();
}

void SamplePublisher::publish(const core::TagSample& sample) {
    auto published =
        client_->publish(tag_topic(sample.path), encode_sample(sample), options_.qos, false);
    if (published) {
        publish_failing_ = false;
        return;
    }
    // Once per streak of failures at warn, then at debug (GW-13).
    const auto& error = published.error();
    if (!publish_failing_) {
        spdlog::warn("publisher: cannot publish {}: {}", sample.path.str(), error.message);
    } else {
        spdlog::debug("publisher: cannot publish {}: {}", sample.path.str(), error.message);
    }
    publish_failing_ = true;
}

void SamplePublisher::arm_heartbeat() {
    if (!running_ || timer_armed_) {
        return;
    }
    const auto due = filter_.next_heartbeat();
    if (!due) {
        return; // disabled, or no tag yet: handle() arms it with the first sample
    }
    // Relative to the injected clock, so a test clock never makes the timer spin.
    timer_.expires_after(
        std::max(*due - clock_->steady_now(), std::chrono::steady_clock::duration{}));
    timer_armed_ = true;
    timer_.async_wait([this](const asio::error_code& ec) {
        timer_armed_ = false;
        if (ec != asio::error::operation_aborted && running_) {
            on_heartbeat();
        }
    });
}

void SamplePublisher::on_heartbeat() {
    const auto gateway_time = clock_->now();
    for (auto& sample : filter_.take_heartbeats(clock_->steady_now())) {
        sample.gateway_time = gateway_time;
        publish(sample);
    }
    arm_heartbeat();
}

} // namespace f2c::messaging
