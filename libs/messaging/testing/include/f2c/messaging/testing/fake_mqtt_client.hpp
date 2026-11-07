#pragma once

#include "f2c/core/error.hpp"
#include "f2c/messaging/mqtt_client.hpp"

#include <chrono>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace f2c::messaging::testing {

// Records every call; thread-safe. publish() and subscribe() succeed unless an error is injected.
class FakeMqttClient final : public MqttClient {
public:
    struct Published {
        std::string topic;
        std::string payload;
        int qos;
        bool retain;

        friend bool operator==(const Published&, const Published&) = default;
    };

    struct Subscription {
        std::string filter;
        int qos;

        friend bool operator==(const Subscription&, const Subscription&) = default;
    };

    void connect() override;
    void disconnect(std::chrono::milliseconds timeout) override;
    core::Expected<void> publish(std::string topic, std::string payload, int qos,
                                 bool retain) override;
    core::Expected<void> subscribe(std::string filter, int qos) override;

    // Makes every later publish() fail with `error` (nullopt: succeed again). Failed publishes
    // are not recorded.
    void fail_publish(std::optional<core::Error> error);

    [[nodiscard]] std::vector<Published> published() const;
    [[nodiscard]] std::vector<Subscription> subscriptions() const;
    [[nodiscard]] int connect_calls() const;
    [[nodiscard]] int disconnect_calls() const;

private:
    mutable std::mutex mutex_;
    std::vector<Published> published_;
    std::vector<Subscription> subscriptions_;
    std::optional<core::Error> publish_error_;
    int connect_calls_{0};
    int disconnect_calls_{0};
};

} // namespace f2c::messaging::testing
