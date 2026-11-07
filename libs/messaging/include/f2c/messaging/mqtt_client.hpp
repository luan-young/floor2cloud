#pragma once

#include "f2c/core/error.hpp"

#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace f2c::messaging {

struct MqttWill {
    std::string topic;
    std::string payload;
    int qos{1};
    bool retain{true};
};

struct MqttConnectOptions {
    std::string uri; // tcp://host:port
    std::string client_id;
    std::chrono::seconds keepalive{10};
    bool clean_session{true};
    std::optional<MqttWill> will;
    std::size_t max_buffered_messages{10'000}; // while disconnected; oldest dropped
    // Reconnect backoff, doubling from min to max. Whole seconds: Paho's automatic reconnect has
    // no finer resolution.
    std::chrono::seconds reconnect_min{1};
    std::chrono::seconds reconnect_max{10};
};

struct MqttMessage {
    std::string topic;
    std::string payload;
    int qos{0};
    bool retained{false};
};

// Handlers run on a library thread; they must not block (post to your own executor) and must not
// throw. Any of them may be empty.
struct MqttHandlers {
    std::function<void()> on_connected; // after every successful (re)connect
    std::function<void(std::string_view cause)> on_connection_lost;
    std::function<void(MqttMessage)> on_message;
};

// Asynchronous client with automatic reconnection, including when the very first connect fails.
// All methods are thread-safe and non-blocking (except disconnect, bounded by its timeout).
class MqttClient {
public:
    MqttClient() = default;
    MqttClient(const MqttClient&) = delete;
    MqttClient& operator=(const MqttClient&) = delete;
    MqttClient(MqttClient&&) = delete;
    MqttClient& operator=(MqttClient&&) = delete;
    virtual ~MqttClient() = default;

    virtual void connect() = 0; // retries until disconnect(); call once
    virtual void disconnect(std::chrono::milliseconds timeout) = 0;
    // Buffered while disconnected (also before the first connect). Error only for invalid
    // arguments or when the library refuses the message.
    virtual core::Expected<void> publish(std::string topic, std::string payload, int qos,
                                         bool retain) = 0;
    // Remembered and re-applied after every reconnect.
    virtual core::Expected<void> subscribe(std::string filter, int qos) = 0;
};

// Paho C++ adapter (ADR-0006). Validates the options (invalid_argument) and creates the client;
// no I/O before connect(). Destroying it closes the connection cleanly, so the broker discards the
// will: it is published only if the process dies or the network fails.
[[nodiscard]] core::Expected<std::unique_ptr<MqttClient>>
make_paho_client(MqttConnectOptions options, MqttHandlers handlers);

} // namespace f2c::messaging
