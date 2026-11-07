#include "f2c/messaging/mqtt_client.hpp"
#include "f2c/messaging/testing/mosquitto_broker.hpp"

#include <catch2/catch_test_macros.hpp>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using f2c::core::Errc;
using f2c::messaging::make_paho_client;
using f2c::messaging::MqttClient;
using f2c::messaging::MqttConnectOptions;
using f2c::messaging::MqttHandlers;
using f2c::messaging::MqttMessage;
using f2c::messaging::MqttWill;
using f2c::messaging::testing::MosquittoBroker;
using namespace std::chrono_literals;

namespace {

bool wait_until(const std::function<bool()>& predicate, std::chrono::milliseconds timeout) {
    const auto end = std::chrono::steady_clock::now() + timeout;
    while (!predicate()) {
        if (std::chrono::steady_clock::now() > end) {
            return false;
        }
        std::this_thread::sleep_for(10ms);
    }
    return true;
}

// Records what the handlers receive.
class Observer {
public:
    MqttHandlers handlers() {
        return MqttHandlers{
            [this] { ++connected_; },
            [this](std::string_view /*cause*/) { ++lost_; },
            [this](MqttMessage message) {
                const std::scoped_lock lock{mutex_};
                messages_.push_back(std::move(message));
            },
        };
    }

    [[nodiscard]] int connected() const { return connected_; }
    [[nodiscard]] int lost() const { return lost_; }

    [[nodiscard]] bool has(const std::string& topic, const std::string& payload) const {
        const std::scoped_lock lock{mutex_};
        return std::ranges::any_of(
            messages_, [&](const auto& m) { return m.topic == topic && m.payload == payload; });
    }

    [[nodiscard]] bool has(const std::string& topic) const {
        const std::scoped_lock lock{mutex_};
        return std::ranges::any_of(messages_, [&](const auto& m) { return m.topic == topic; });
    }

    // The first message received on `topic`; an empty message if there is none.
    [[nodiscard]] MqttMessage get(const std::string& topic) const {
        const std::scoped_lock lock{mutex_};
        const auto it =
            std::ranges::find_if(messages_, [&](const auto& m) { return m.topic == topic; });
        return it != messages_.end() ? *it : MqttMessage{};
    }

private:
    std::atomic<int> connected_{0};
    std::atomic<int> lost_{0};
    mutable std::mutex mutex_;
    std::vector<MqttMessage> messages_;
};

MqttConnectOptions options(const std::string& uri, std::string client_id) {
    MqttConnectOptions o;
    o.uri = uri;
    o.client_id = std::move(client_id);
    o.keepalive = 2s;
    return o;
}

std::unique_ptr<MqttClient> make(MqttConnectOptions o, Observer& observer) {
    auto client = make_paho_client(std::move(o), observer.handlers());
    REQUIRE(client.has_value());
    return std::move(client).value();
}

// Returns once `client` receives its own publish on `topic`: connected, and the subscription
// applied by the connected handler has reached the broker.
bool wait_subscribed(MqttClient& client, const Observer& observer, const std::string& topic) {
    return wait_until(
        [&] {
            (void)client.publish(topic, "probe", 1, false);
            return observer.has(topic, "probe");
        },
        5s);
}

// A connected client subscribed to `topic`.
struct Watcher {
    Watcher(const MosquittoBroker& broker, const std::string& topic)
        : client{make(options(broker.uri(), "watcher"), observer)} {
        client->connect();
        REQUIRE(client->subscribe(topic, 1).has_value());
        REQUIRE(wait_subscribed(*client, observer, topic));
    }

    Observer observer;
    std::unique_ptr<MqttClient> client;
};

} // namespace

TEST_CASE("PahoMqttClient connects, subscribes and receives its own publish", "[mqtt][component]") {
    const MosquittoBroker broker;
    Observer observer;
    auto client = make(options(broker.uri(), "roundtrip"), observer);

    client->connect();
    REQUIRE(client->subscribe("plant1/value", 1).has_value());
    REQUIRE(wait_until([&] { return observer.connected() == 1; }, 5s));
    REQUIRE(wait_subscribed(*client, observer, "plant1/value"));
    REQUIRE(client->publish("plant1/value", "42", 1, false).has_value());
    REQUIRE(wait_until([&] { return observer.has("plant1/value", "42"); }, 5s));

    const auto message = observer.get("plant1/value");
    CHECK(message.qos == 1);
    CHECK_FALSE(message.retained);

    client->disconnect(2s);
    CHECK(observer.lost() == 0); // a clean disconnect is not a lost connection
}

TEST_CASE("PahoMqttClient buffers publishes made before the first connect", "[mqtt][component]") {
    const MosquittoBroker broker;
    const Watcher watcher{broker, "plant1/value"};
    Observer observer;
    auto publisher = make(options(broker.uri(), "publisher"), observer);

    REQUIRE(publisher->publish("plant1/value", "early", 1, false).has_value());
    publisher->connect();

    CHECK(wait_until([&] { return watcher.observer.has("plant1/value", "early"); }, 5s));
    publisher->disconnect(2s);
}

// Not run directly: the will test starts this binary again with this tag, and the child process
// connects with a will and then dies without disconnecting, like a killed gateway.
TEST_CASE("PahoMqttClient will child process", "[.][will-child]") {
    const char* uri = std::getenv("F2C_MQTT_WILL_URI");     // NOLINT(concurrency-mt-unsafe)
    const char* topic = std::getenv("F2C_MQTT_WILL_TOPIC"); // NOLINT(concurrency-mt-unsafe)
    REQUIRE(uri != nullptr);
    REQUIRE(topic != nullptr);
    Observer observer;
    auto o = options(uri, "dying");
    o.will = MqttWill{topic, "offline", 1, true};
    auto client = make(o, observer);
    client->connect();
    REQUIRE(wait_until([&] { return observer.connected() == 1; }, 5s));
    std::_Exit(0); // no DISCONNECT, no destructors: the broker sees the socket close
}

TEST_CASE("PahoMqttClient registers a retained will", "[mqtt][component]") {
    const MosquittoBroker broker;
    const Watcher watcher{broker, "plant1/status"};

    const auto child = "F2C_MQTT_WILL_TOPIC=plant1/status F2C_MQTT_WILL_URI=" + broker.uri() +
                       " /proc/" + std::to_string(::getpid()) + "/exe '[will-child]' > /dev/null";
    REQUIRE(std::system(child.c_str()) == 0); // NOLINT(concurrency-mt-unsafe,cert-env33-c)

    REQUIRE(wait_until([&] { return watcher.observer.has("plant1/status", "offline"); }, 5s));

    // A late subscriber gets it as a retained message.
    Observer late_observer;
    auto late = make(options(broker.uri(), "late"), late_observer);
    late->connect();
    REQUIRE(late->subscribe("plant1/status", 1).has_value());
    REQUIRE(wait_until([&] { return late_observer.has("plant1/status", "offline"); }, 5s));
    CHECK(late_observer.get("plant1/status").retained);
    late->disconnect(2s);
}

TEST_CASE("PahoMqttClient sends no will after a clean disconnect", "[mqtt][component]") {
    const MosquittoBroker broker;
    const Watcher watcher{broker, "plant1/status"};
    Observer observer;
    auto o = options(broker.uri(), "clean");
    o.will = MqttWill{"plant1/status", "offline", 1, true};
    auto client = make(o, observer);
    client->connect();
    REQUIRE(wait_until([&] { return observer.connected() == 1; }, 5s));

    client->disconnect(2s);
    // Anything the broker sent before this publish has arrived once it does (same session, QoS 1).
    REQUIRE(watcher.client->publish("plant1/status", "marker", 1, false).has_value());
    REQUIRE(wait_until([&] { return watcher.observer.has("plant1/status", "marker"); }, 5s));

    CHECK_FALSE(watcher.observer.has("plant1/status", "offline"));
}

TEST_CASE("PahoMqttClient keeps retrying the first connect until disconnect", "[mqtt][component]") {
    MosquittoBroker broker;
    broker.stop();
    Observer observer;
    auto client = make(options(broker.uri(), "unreachable"), observer);

    client->connect();
    std::this_thread::sleep_for(1500ms); // the subject: a failed attempt, then a 1 s backoff
    const auto start = std::chrono::steady_clock::now();
    client->disconnect(2s);

    CHECK(std::chrono::steady_clock::now() - start < 1s);
    CHECK(observer.connected() == 0);
}

TEST_CASE("PahoMqttClient connects once a broker that was down at start comes up",
          "[mqtt][component]") {
    MosquittoBroker broker;
    broker.stop();
    Observer observer;
    auto client = make(options(broker.uri(), "late-broker"), observer);
    REQUIRE(client->subscribe("plant1/value", 1).has_value());
    REQUIRE(client->publish("plant1/value", "buffered", 1, false).has_value());
    client->connect();
    std::this_thread::sleep_for(1500ms); // the subject: failed attempts before the broker is up

    broker.start();

    REQUIRE(wait_until([&] { return observer.connected() == 1; }, 15s));
    CHECK(wait_subscribed(*client, observer, "plant1/value"));
    client->disconnect(2s);
}

TEST_CASE("PahoMqttClient reconnects and re-subscribes after the broker restarts",
          "[mqtt][component]") {
    MosquittoBroker broker;
    Observer observer;
    auto client = make(options(broker.uri(), "restart"), observer);
    client->connect();
    REQUIRE(client->subscribe("plant1/value", 1).has_value());
    REQUIRE(wait_subscribed(*client, observer, "plant1/value"));

    broker.stop(); // no persistence: the new broker knows nothing of the old subscription
    REQUIRE(wait_until([&] { return observer.lost() == 1; }, 5s));
    broker.start();

    REQUIRE(wait_until([&] { return observer.connected() == 2; }, 15s));
    REQUIRE(client->publish("plant1/value", "after restart", 1, false).has_value());
    CHECK(wait_until([&] { return observer.has("plant1/value", "after restart"); }, 5s));
    client->disconnect(2s);
}

TEST_CASE("PahoMqttClient rejects invalid arguments", "[mqtt][component]") {
    Observer observer;
    auto client = make(options("tcp://127.0.0.1:1", "invalid"), observer); // no I/O needed
    const auto rejected = [](const f2c::core::Expected<void>& result) {
        return !result.has_value() && result.error().code == Errc::invalid_argument;
    };

    CHECK(rejected(client->publish("", "x", 1, false)));
    CHECK(rejected(client->publish("a/+/b", "x", 1, false)));
    CHECK(rejected(client->publish("a/b", "x", 3, false)));
    CHECK(rejected(client->subscribe("", 1)));
    CHECK(rejected(client->subscribe("a/#", -1)));
}

TEST_CASE("make_paho_client rejects invalid options", "[mqtt][component]") {
    auto o = options("tcp://127.0.0.1:1", "bad");
    SECTION("uri") {
        o.uri = "not a uri";
    }
    SECTION("uri without host") {
        o.uri = "tcp://";
    }
    SECTION("client id") {
        o.client_id.clear();
    }
    SECTION("reconnect range") {
        o.reconnect_min = o.reconnect_max + 1s;
    }
    SECTION("zero reconnect") {
        o.reconnect_min = 0s;
    }
    SECTION("will topic") {
        o.will = MqttWill{"a/#", "offline", 1, true};
    }
    SECTION("will qos") {
        o.will = MqttWill{"a/b", "offline", 5, true};
    }
    SECTION("buffer") {
        o.max_buffered_messages = 0;
    }

    const auto client = make_paho_client(o, {});

    REQUIRE_FALSE(client.has_value());
    CHECK(client.error().code == Errc::invalid_argument);
}
