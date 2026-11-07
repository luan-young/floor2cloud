#include "f2c/messaging/testing/fake_mqtt_client.hpp"

#include <utility>

namespace f2c::messaging::testing {

void FakeMqttClient::connect() {
    const std::scoped_lock lock{mutex_};
    ++connect_calls_;
}

void FakeMqttClient::disconnect(std::chrono::milliseconds /*timeout*/) {
    const std::scoped_lock lock{mutex_};
    ++disconnect_calls_;
}

core::Expected<void> FakeMqttClient::publish(std::string topic, std::string payload, int qos,
                                             bool retain) {
    const std::scoped_lock lock{mutex_};
    if (publish_error_) {
        return tl::unexpected(*publish_error_);
    }
    published_.push_back(Published{std::move(topic), std::move(payload), qos, retain});
    return {};
}

core::Expected<void> FakeMqttClient::subscribe(std::string filter, int qos) {
    const std::scoped_lock lock{mutex_};
    subscriptions_.push_back(Subscription{std::move(filter), qos});
    return {};
}

void FakeMqttClient::fail_publish(std::optional<core::Error> error) {
    const std::scoped_lock lock{mutex_};
    publish_error_ = std::move(error);
}

std::vector<FakeMqttClient::Published> FakeMqttClient::published() const {
    const std::scoped_lock lock{mutex_};
    return published_;
}

std::vector<FakeMqttClient::Subscription> FakeMqttClient::subscriptions() const {
    const std::scoped_lock lock{mutex_};
    return subscriptions_;
}

int FakeMqttClient::connect_calls() const {
    const std::scoped_lock lock{mutex_};
    return connect_calls_;
}

int FakeMqttClient::disconnect_calls() const {
    const std::scoped_lock lock{mutex_};
    return disconnect_calls_;
}

} // namespace f2c::messaging::testing
