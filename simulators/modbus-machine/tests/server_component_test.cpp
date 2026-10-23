// Component tests (label `component`): run_server in-process on an ephemeral loopback port, driven
// by a real libmodbus client. They cover only the I/O shell; the model's behaviour over time is
// covered by the unit tests. The step period is long, so the model does not move during a test and
// every assertion is about an immediate effect.
#include <catch2/catch_test_macros.hpp>
#include <modbus.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <thread>

#include "options.hpp"
#include "server.hpp"

using namespace std::chrono_literals;
using f2c::sim::run_server;
using f2c::sim::SimulatorOptions;

namespace {

constexpr auto startup_timeout = 5s;

// Polls until `predicate` holds or `timeout` expires; never a bare sleep.
bool wait_until(const std::function<bool()>& predicate, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        std::this_thread::sleep_for(10ms);
    }
    return true;
}

SimulatorOptions test_options() {
    SimulatorOptions options;
    options.bind_address = "127.0.0.1";
    options.port = 0; // ephemeral: tests may run in parallel
    options.step_period = 60s;
    return options;
}

// Runs the server on a thread for the lifetime of the object.
class RunningServer {
public:
    explicit RunningServer(SimulatorOptions options = test_options()) {
        auto listening = port_promise_.get_future();
        thread_ = std::thread{[this, options = std::move(options)] {
            bool reported = false;
            result_ = run_server(options, 7, stop_, [&](std::uint16_t port) {
                reported = true;
                port_promise_.set_value(port);
            });
            if (!reported) {
                port_promise_.set_value(0); // failed before listening: unblock the constructor
            }
        }};
        if (listening.wait_for(startup_timeout) == std::future_status::ready) {
            port_ = listening.get();
        }
    }
    RunningServer(const RunningServer&) = delete;
    RunningServer& operator=(const RunningServer&) = delete;
    RunningServer(RunningServer&&) = delete;
    RunningServer& operator=(RunningServer&&) = delete;
    ~RunningServer() { stop(); }

    [[nodiscard]] std::uint16_t port() const { return port_; }

    // Requests a stop and waits for run_server to return; returns how long that took.
    std::chrono::steady_clock::duration stop() {
        const auto start = std::chrono::steady_clock::now();
        stop_.store(true);
        if (thread_.joinable()) {
            thread_.join();
        }
        return std::chrono::steady_clock::now() - start;
    }

    // run_server's result; meaningful after stop().
    [[nodiscard]] const f2c::core::Expected<void>& result() const { return result_; }

private:
    std::atomic<bool> stop_{false};
    std::promise<std::uint16_t> port_promise_;
    std::uint16_t port_{0};
    f2c::core::Expected<void> result_;
    std::thread thread_;
};

struct ContextDeleter {
    void operator()(modbus_t* ctx) const noexcept {
        modbus_close(ctx);
        modbus_free(ctx);
    }
};
using Client = std::unique_ptr<modbus_t, ContextDeleter>;

Client connect(std::uint16_t port) {
    Client client{modbus_new_tcp("127.0.0.1", port)};
    REQUIRE(client);
    modbus_set_slave(client.get(), 1);
    modbus_set_response_timeout(client.get(), 1, 0);
    REQUIRE(modbus_connect(client.get()) == 0);
    return client;
}

std::array<std::uint16_t, 5> read_holding(modbus_t* client) {
    std::array<std::uint16_t, 5> registers{};
    REQUIRE(modbus_read_registers(client, 0, static_cast<int>(registers.size()),
                                  registers.data()) == static_cast<int>(registers.size()));
    return registers;
}

bool read_fault_coil(modbus_t* client) {
    std::uint8_t bit = 0;
    REQUIRE(modbus_read_bits(client, 0, 1, &bit) == 1);
    return bit != 0;
}

constexpr std::size_t speed = 2;
constexpr std::size_t speed_setpoint = 3;
constexpr std::size_t state = 4;
constexpr std::uint16_t running = 1;
constexpr std::uint16_t faulted = 2;

} // namespace

TEST_CASE("Server reports the port it listens on", "[sim][server]") {
    const RunningServer server;

    CHECK(server.port() != 0);
}

TEST_CASE("Server serves the register map", "[sim][server]") {
    const RunningServer server;
    REQUIRE(server.port() != 0);
    const auto client = connect(server.port());

    const auto holding = read_holding(client.get());
    CHECK(holding[0] == 0); // counter, high word
    CHECK(holding[1] == 0); // counter, low word
    CHECK(holding[speed] == 0);
    CHECK(holding[speed_setpoint] == 120);
    CHECK(holding[state] == running);

    std::uint16_t temperature = 0;
    REQUIRE(modbus_read_input_registers(client.get(), 0, 1, &temperature) == 1);
    CHECK(temperature >= 230); // ~25 °C idle, in 0.1 °C
    CHECK(temperature <= 270);

    CHECK_FALSE(read_fault_coil(client.get()));
}

TEST_CASE("Server forwards a setpoint write to the model", "[sim][server]") {
    const RunningServer server;
    REQUIRE(server.port() != 0);
    const auto client = connect(server.port());

    REQUIRE(modbus_write_register(client.get(), static_cast<int>(speed_setpoint), 200) == 1);
    CHECK(read_holding(client.get())[speed_setpoint] == 200);

    REQUIRE(modbus_write_register(client.get(), static_cast<int>(speed_setpoint), 1000) == 1);
    CHECK(read_holding(client.get())[speed_setpoint] == 300); // clamped by the model
}

TEST_CASE("Server overwrites writes to read-only registers at once", "[sim][server]") {
    const RunningServer server;
    REQUIRE(server.port() != 0);
    const auto client = connect(server.port());

    REQUIRE(modbus_write_register(client.get(), static_cast<int>(speed), 250) == 1);
    REQUIRE(modbus_write_register(client.get(), static_cast<int>(state), faulted) == 1);

    const auto holding = read_holding(client.get());
    CHECK(holding[speed] == 0);
    CHECK(holding[state] == running);
}

TEST_CASE("Server forwards the fault coil to the model", "[sim][server]") {
    const RunningServer server;
    REQUIRE(server.port() != 0);
    const auto client = connect(server.port());

    REQUIRE(modbus_write_bit(client.get(), 0, 1) == 1);
    CHECK(read_fault_coil(client.get()));
    CHECK(read_holding(client.get())[state] == faulted);

    REQUIRE(modbus_write_bit(client.get(), 0, 0) == 1);
    CHECK_FALSE(read_fault_coil(client.get()));
    CHECK(read_holding(client.get())[state] == running);
}

TEST_CASE("Server answers an address outside the map with an exception", "[sim][server]") {
    const RunningServer server;
    REQUIRE(server.port() != 0);
    const auto client = connect(server.port());

    std::uint16_t value = 0;
    CHECK(modbus_read_registers(client.get(), 5, 1, &value) == -1);
    CHECK(errno == EMBXILADD);

    // The connection is kept after an exception.
    CHECK(read_holding(client.get())[speed_setpoint] == 120);
}

TEST_CASE("Server serves several clients at once", "[sim][server]") {
    const RunningServer server;
    REQUIRE(server.port() != 0);
    const auto first = connect(server.port());
    const auto second = connect(server.port());

    REQUIRE(modbus_write_register(first.get(), static_cast<int>(speed_setpoint), 150) == 1);
    CHECK(read_holding(second.get())[speed_setpoint] == 150);

    REQUIRE(modbus_write_register(second.get(), static_cast<int>(speed_setpoint), 90) == 1);
    CHECK(read_holding(first.get())[speed_setpoint] == 90);
}

TEST_CASE("Server keeps serving after a client disconnects", "[sim][server]") {
    const RunningServer server;
    REQUIRE(server.port() != 0);
    {
        const auto gone = connect(server.port());
        REQUIRE(modbus_write_register(gone.get(), static_cast<int>(speed_setpoint), 60) == 1);
    }

    const auto client = connect(server.port());
    CHECK(read_holding(client.get())[speed_setpoint] == 60);
}

TEST_CASE("Server touches the health file", "[sim][server]") {
    const auto path = std::filesystem::temp_directory_path() /
                      ("f2c-sim-health-" + std::to_string(::getpid()) + ".tmp");
    std::filesystem::remove(path);
    auto options = test_options();
    options.health_file = path;

    {
        const RunningServer server{options};
        REQUIRE(server.port() != 0);
        CHECK(wait_until([&] { return std::filesystem::exists(path); }, 2s));
    }
    std::filesystem::remove(path);
}

TEST_CASE("Server stops promptly when asked", "[sim][server]") {
    RunningServer server;
    REQUIRE(server.port() != 0);
    const auto client = connect(server.port()); // a connected client must not delay the stop

    const auto elapsed = server.stop();

    CHECK(elapsed < 1s); // the step period is 60 s: stopping must not wait for the next step
    CHECK(server.result().has_value());
}

TEST_CASE("Server fails cleanly when the port is taken", "[sim][server]") {
    const RunningServer first;
    REQUIRE(first.port() != 0);
    auto options = test_options();
    options.port = first.port();

    RunningServer second{options};
    second.stop();

    CHECK(second.port() == 0);
    REQUIRE_FALSE(second.result().has_value());
    CHECK(second.result().error().code == f2c::core::Errc::io_error);
}
