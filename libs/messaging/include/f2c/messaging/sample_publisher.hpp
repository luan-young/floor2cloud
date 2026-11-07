#pragma once

#include "f2c/core/clock.hpp"
#include "f2c/core/tag_sample.hpp"
#include "f2c/messaging/change_filter.hpp"
#include "f2c/messaging/mqtt_client.hpp"

#include <asio/any_io_executor.hpp>
#include <asio/steady_timer.hpp>

#include <chrono>
#include <span>

namespace f2c::messaging {

struct PublisherOptions {
    int qos{1};
    std::chrono::seconds heartbeat{30}; // 0 disables
};

// Publishes tag samples per spec § 6.3: stamps gateway_time, applies the ChangeFilter and
// publishes to tag_topic() (not retained). Heartbeats come from a timer armed for the earliest
// one due.
//
// All state lives on `executor`, which must run on a single thread (ADR-0010). submit() is the
// only method callable from any thread. Destroy it only once the executor no longer runs its
// handlers (e.g. after io_context::run() has returned).
class SamplePublisher {
public:
    SamplePublisher(const asio::any_io_executor& executor, MqttClient& client,
                    PublisherOptions options, const core::Clock& clock = core::system_clock());

    // Copies the samples and posts them to the executor, which stamps gateway_time, applies the
    // ChangeFilter and publishes.
    void submit(std::span<const core::TagSample> samples);

    void start(); // enables heartbeats; call from the executor
    void stop();  // disables them and cancels the timer; call from the executor

private:
    void handle(std::vector<core::TagSample> samples);
    void publish(const core::TagSample& sample);
    void arm_heartbeat();
    void on_heartbeat();

    asio::any_io_executor executor_;
    MqttClient* client_;
    PublisherOptions options_;
    const core::Clock* clock_;
    ChangeFilter filter_;
    asio::steady_timer timer_;
    bool running_{false};
    bool timer_armed_{false};
    bool publish_failing_{false};
};

} // namespace f2c::messaging
