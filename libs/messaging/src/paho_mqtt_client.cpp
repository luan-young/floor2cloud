#include "f2c/core/backoff.hpp"
#include "f2c/messaging/mqtt_client.hpp"

#include <mqtt/async_client.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <condition_variable>
#include <exception>
#include <limits>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace f2c::messaging {

namespace {

constexpr auto connect_timeout = std::chrono::seconds{5};

// TLS schemes come with Milestone 7.
bool valid_uri(std::string_view uri) noexcept {
    constexpr std::array<std::string_view, 2> schemes{"tcp://", "mqtt://"};
    return std::ranges::any_of(schemes, [uri](std::string_view scheme) {
        return uri.starts_with(scheme) && uri.size() > scheme.size();
    });
}

bool valid_qos(int qos) noexcept {
    return qos >= 0 && qos <= 2;
}

bool has_wildcard(std::string_view topic) noexcept {
    return topic.find_first_of("+#") != std::string_view::npos;
}

core::Error to_error(const mqtt::exception& e) {
    return core::Error{core::Errc::invalid_argument, e.what()};
}

// Runs a user handler on a Paho thread; nothing may escape into Paho (NF-6).
template <typename F>
void guarded(std::string_view what, F&& f) noexcept {
    try {
        std::forward<F>(f)();
    } catch (const std::exception& e) {
        spdlog::error("mqtt: {} handler threw: {}", what, e.what());
    } catch (...) {
        spdlog::error("mqtt: {} handler threw", what);
    }
}

// Paho C++ adapter (ADR-0006). Paho reconnects on its own only after a first successful connect,
// so the first connect is retried here, on a thread of our own, with the same backoff policy.
// Subscriptions are re-applied after every (re)connect.
class PahoMqttClient final : public MqttClient, private mqtt::iaction_listener {
public:
    PahoMqttClient(MqttConnectOptions options, MqttHandlers handlers,
                   std::unique_ptr<mqtt::async_client> client)
        : options_{std::move(options)}, handlers_{std::move(handlers)}, client_{std::move(client)} {
        auto builder = mqtt::connect_options_builder()
                           .clean_session(options_.clean_session)
                           .keep_alive_interval(options_.keepalive)
                           .connect_timeout(connect_timeout)
                           .automatic_reconnect(options_.reconnect_min, options_.reconnect_max);
        if (options_.will) {
            builder.will(mqtt::message(options_.will->topic, options_.will->payload,
                                       options_.will->qos, options_.will->retain));
        }
        connect_options_ = builder.finalize();

        client_->set_connected_handler([this](const std::string& /*cause*/) { on_connected(); });
        client_->set_connection_lost_handler([this](const std::string& cause) {
            if (handlers_.on_connection_lost) {
                guarded("connection lost", [&] { handlers_.on_connection_lost(cause); });
            }
        });
        client_->set_message_callback([this](const mqtt::const_message_ptr& message) {
            if (handlers_.on_message && message) {
                guarded("message", [&] {
                    handlers_.on_message(MqttMessage{message->get_topic(),
                                                     message->get_payload_str(), message->get_qos(),
                                                     message->is_retained()});
                });
            }
        });
    }

    PahoMqttClient(const PahoMqttClient&) = delete;
    PahoMqttClient& operator=(const PahoMqttClient&) = delete;
    PahoMqttClient(PahoMqttClient&&) = delete;
    PahoMqttClient& operator=(PahoMqttClient&&) = delete;

    ~PahoMqttClient() override { stop_connecting(); }

    void connect() override {
        const std::scoped_lock lock{mutex_};
        if (connector_.joinable() || stopping_) {
            return;
        }
        connector_ = std::thread{[this] { connect_until_done(); }};
    }

    void disconnect(std::chrono::milliseconds timeout) override {
        stop_connecting();
        try {
            // Also stops Paho's automatic reconnect when the connection is down.
            client_->disconnect(timeout)->wait_for(timeout);
        } catch (const mqtt::exception& e) {
            spdlog::debug("mqtt: disconnect from {}: {}", options_.uri, e.what());
        }
    }

    core::Expected<void> publish(std::string topic, std::string payload, int qos,
                                 bool retain) override {
        if (topic.empty() || has_wildcard(topic) || !valid_qos(qos)) {
            return core::make_error(core::Errc::invalid_argument, "publish: invalid topic '" +
                                                                      topic + "' or QoS " +
                                                                      std::to_string(qos));
        }
        try {
            client_->publish(mqtt::make_message(std::move(topic), std::move(payload), qos, retain));
        } catch (const mqtt::exception& e) {
            return tl::unexpected(to_error(e));
        }
        return {};
    }

    core::Expected<void> subscribe(std::string filter, int qos) override {
        if (filter.empty() || !valid_qos(qos)) {
            return core::make_error(core::Errc::invalid_argument, "subscribe: invalid filter '" +
                                                                      filter + "' or QoS " +
                                                                      std::to_string(qos));
        }
        {
            const std::scoped_lock lock{mutex_};
            subscriptions_.emplace_back(filter, qos);
        }
        if (client_->is_connected()) {
            apply_subscription(filter, qos);
        }
        return {};
    }

private:
    void connect_until_done() {
        core::Backoff backoff{core::BackoffPolicy{options_.reconnect_min, options_.reconnect_max}};
        bool reported = false;
        std::unique_lock lock{mutex_};
        while (!stopping_) {
            attempt_ = Attempt::pending;
            lock.unlock();
            try {
                client_->connect(connect_options_, nullptr, *this);
            } catch (const mqtt::exception& e) {
                on_failure_message(e.what());
            }
            lock.lock();
            changed_.wait(lock, [this] { return stopping_ || attempt_ != Attempt::pending; });
            if (stopping_ || attempt_ == Attempt::succeeded) {
                return; // Paho's automatic reconnect takes over
            }
            const auto delay = backoff.next_delay();
            if (!reported) {
                spdlog::warn("mqtt: cannot connect to {}: {}; retrying", options_.uri,
                             failure_reason_);
                reported = true;
            } else {
                spdlog::debug("mqtt: connect to {} failed: {}; retrying in {} ms", options_.uri,
                              failure_reason_, delay.count());
            }
            changed_.wait_for(lock, delay, [this] { return stopping_; });
        }
    }

    void stop_connecting() {
        {
            const std::scoped_lock lock{mutex_};
            stopping_ = true;
        }
        changed_.notify_all();
        if (connector_.joinable() && connector_.get_id() != std::this_thread::get_id()) {
            connector_.join();
        }
    }

    // iaction_listener, for the first connect only.
    void on_success(const mqtt::token& /*token*/) override {
        {
            const std::scoped_lock lock{mutex_};
            attempt_ = Attempt::succeeded;
        }
        changed_.notify_all();
    }

    void on_failure(const mqtt::token& token) override {
        auto reason = token.get_error_message();
        if (reason.empty()) {
            reason = mqtt::exception::error_str(token.get_return_code());
        }
        on_failure_message(std::move(reason));
    }

    void on_failure_message(std::string reason) {
        {
            const std::scoped_lock lock{mutex_};
            attempt_ = Attempt::failed;
            failure_reason_ = std::move(reason);
        }
        changed_.notify_all();
    }

    void on_connected() {
        std::vector<std::pair<std::string, int>> subscriptions;
        {
            const std::scoped_lock lock{mutex_};
            subscriptions = subscriptions_;
        }
        for (const auto& [filter, qos] : subscriptions) {
            apply_subscription(filter, qos);
        }
        if (handlers_.on_connected) {
            guarded("connected", [&] { handlers_.on_connected(); });
        }
    }

    void apply_subscription(const std::string& filter, int qos) noexcept {
        try {
            client_->subscribe(filter, qos);
        } catch (const mqtt::exception& e) {
            // Retried on the next reconnect.
            spdlog::warn("mqtt: cannot subscribe to {}: {}", filter, e.what());
        }
    }

    enum class Attempt : std::uint8_t { pending, succeeded, failed };

    MqttConnectOptions options_;
    MqttHandlers handlers_;
    mqtt::connect_options connect_options_;

    std::mutex mutex_;
    std::condition_variable changed_;
    std::thread connector_;
    bool stopping_{false};
    Attempt attempt_{Attempt::pending};
    std::string failure_reason_;
    std::vector<std::pair<std::string, int>> subscriptions_;

    // Last, so it is destroyed first: Paho's callbacks use the members above until it is gone.
    std::unique_ptr<mqtt::async_client> client_;
};

} // namespace

core::Expected<std::unique_ptr<MqttClient>> make_paho_client(MqttConnectOptions options,
                                                             MqttHandlers handlers) {
    if (!valid_uri(options.uri)) {
        return core::make_error(core::Errc::invalid_argument,
                                "mqtt: '" + options.uri + "' is not tcp://host:port");
    }
    if (options.client_id.empty()) {
        return core::make_error(core::Errc::invalid_argument, "mqtt: empty client id");
    }
    if (options.reconnect_min <= std::chrono::seconds::zero() ||
        options.reconnect_min > options.reconnect_max) {
        return core::make_error(core::Errc::invalid_argument,
                                "mqtt: need 0 < reconnect_min <= reconnect_max");
    }
    if (options.will && (options.will->topic.empty() || has_wildcard(options.will->topic) ||
                         !valid_qos(options.will->qos))) {
        return core::make_error(core::Errc::invalid_argument, "mqtt: invalid will");
    }
    if (options.max_buffered_messages == 0 ||
        options.max_buffered_messages > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return core::make_error(core::Errc::invalid_argument,
                                "mqtt: max_buffered_messages out of range");
    }
    try {
        auto create = mqtt::create_options_builder()
                          .server_uri(options.uri)
                          .client_id(options.client_id)
                          .send_while_disconnected(true, true) // also before the first connect
                          .max_buffered_messages(static_cast<int>(options.max_buffered_messages))
                          .delete_oldest_messages()
                          .finalize();
        auto client = std::make_unique<mqtt::async_client>(create);
        return std::make_unique<PahoMqttClient>(std::move(options), std::move(handlers),
                                                std::move(client));
    } catch (const mqtt::exception& e) {
        return core::make_error(core::Errc::invalid_argument, "mqtt: cannot create client for '" +
                                                                  options.uri + "': " + e.what());
    }
}

} // namespace f2c::messaging
