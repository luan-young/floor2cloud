// Component tests (label `component`): LibmodbusClient and ModbusDriver against the simulator's
// libmodbus server running in-process on an ephemeral loopback port (spec § 9.1, ADR-0003). They
// cover only what unit tests cannot: wiring, wire format, error mapping and the Asio shell. The
// scanning and reconnection rules are covered by the Poller unit tests.
#include "f2c/drivers/modbus/modbus_client.hpp"
#include "f2c/drivers/modbus/modbus_driver.hpp"

#include <arpa/inet.h>
#include <asio/executor_work_guard.hpp>
#include <asio/io_context.hpp>
#include <catch2/catch_test_macros.hpp>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include "options.hpp"
#include "server.hpp"

using namespace std::chrono_literals;
using f2c::config::ByteOrder;
using f2c::config::DeviceConfig;
using f2c::config::LinearScaling;
using f2c::config::ModbusAddress;
using f2c::config::ModbusArea;
using f2c::config::ModbusTcpConfig;
using f2c::config::ModbusType;
using f2c::config::TagConfig;
using f2c::config::WordOrder;
using f2c::core::Errc;
using f2c::core::Quality;
using f2c::core::TagSample;
using f2c::drivers::DriverState;
using f2c::drivers::modbus::make_libmodbus_client;
using f2c::drivers::modbus::ModbusDriver;

namespace {

constexpr auto deadline = 5s;

// Polls until `predicate` holds or `timeout` expires; never a bare sleep.
bool wait_until(const std::function<bool()>& predicate, std::chrono::milliseconds timeout) {
    const auto end = std::chrono::steady_clock::now() + timeout;
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= end) {
            return false;
        }
        std::this_thread::sleep_for(10ms);
    }
    return true;
}

// The simulator on a thread for the lifetime of the object; a 60 s step keeps the model still.
class Simulator {
public:
    Simulator() {
        auto listening = port_promise_.get_future();
        thread_ = std::thread{[this] {
            f2c::sim::SimulatorOptions options;
            options.bind_address = "127.0.0.1";
            options.port = 0;
            options.step_period = 60s;
            bool reported = false;
            (void)f2c::sim::run_server(options, 7, stop_, [&](std::uint16_t port) {
                reported = true;
                port_promise_.set_value(port);
            });
            if (!reported) {
                port_promise_.set_value(0);
            }
        }};
        if (listening.wait_for(deadline) == std::future_status::ready) {
            port_ = listening.get();
        }
        REQUIRE(port_ != 0);
    }
    Simulator(const Simulator&) = delete;
    Simulator& operator=(const Simulator&) = delete;
    Simulator(Simulator&&) = delete;
    Simulator& operator=(Simulator&&) = delete;
    ~Simulator() { stop(); }

    [[nodiscard]] std::uint16_t port() const { return port_; }

    void stop() {
        stop_.store(true);
        if (thread_.joinable()) {
            thread_.join();
        }
    }

private:
    std::atomic<bool> stop_{false};
    std::promise<std::uint16_t> port_promise_;
    std::uint16_t port_{0};
    std::thread thread_;
};

// A loopback socket that listens but never accepts or answers: the kernel completes the TCP
// handshake, so connect() succeeds and every request times out. Closed, it frees a port that
// refuses connections.
class SilentSocket {
public:
    SilentSocket() : fd_{::socket(AF_INET, SOCK_STREAM, 0)} {
        REQUIRE(fd_ >= 0);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): the BSD socket API
        auto* generic = reinterpret_cast<sockaddr*>(&address);
        REQUIRE(::bind(fd_, generic, sizeof address) == 0);
        REQUIRE(::listen(fd_, 4) == 0);
        socklen_t length = sizeof address;
        REQUIRE(::getsockname(fd_, generic, &length) == 0);
        port_ = ntohs(address.sin_port);
    }
    SilentSocket(const SilentSocket&) = delete;
    SilentSocket& operator=(const SilentSocket&) = delete;
    SilentSocket(SilentSocket&&) = delete;
    SilentSocket& operator=(SilentSocket&&) = delete;
    ~SilentSocket() { close(); }

    [[nodiscard]] std::uint16_t port() const { return port_; }

    void close() {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }

private:
    int fd_;
    std::uint16_t port_{0};
};

std::uint16_t closed_port() {
    const SilentSocket socket;
    return socket.port(); // closed on return: nothing listens there any more
}

ModbusTcpConfig tcp_config(std::uint16_t port, std::chrono::milliseconds timeout = 500ms) {
    return ModbusTcpConfig{.host = "127.0.0.1",
                           .port = port,
                           .unit_id = 1,
                           .timeout = timeout,
                           .reconnect = {.initial = 50ms, .max = 200ms},
                           .byte_order = ByteOrder::big,
                           .word_order = WordOrder::big};
}

TagConfig tag(const std::string& name, ModbusArea area, std::uint16_t address, ModbusType type,
              std::optional<LinearScaling> scaling = std::nullopt) {
    auto path = f2c::core::TagPath::parse("plant1/production/line1/cell-c/filler/" + name);
    REQUIRE(path);
    return TagConfig{.name = name,
                     .path = *std::move(path),
                     .scan_period = 50ms,
                     .scaling = scaling,
                     .address = ModbusAddress{.area = area,
                                              .address = address,
                                              .type = type,
                                              .byte_order = ByteOrder::big,
                                              .word_order = WordOrder::big}};
}

// The § 5.1 filler with a 50 ms scan.
DeviceConfig filler(std::uint16_t port) {
    auto path = f2c::core::TagPath::parse("plant1/production/line1/cell-c/filler");
    REQUIRE(path);
    return DeviceConfig{.id = "filler",
                        .path = *std::move(path),
                        .driver = tcp_config(port, 200ms),
                        .tags = {tag("counter", ModbusArea::holding, 0, ModbusType::uint32),
                                 tag("speed", ModbusArea::holding, 2, ModbusType::uint16),
                                 tag("speed_setpoint", ModbusArea::holding, 3, ModbusType::uint16),
                                 tag("state", ModbusArea::holding, 4, ModbusType::uint16),
                                 tag("temperature", ModbusArea::input, 0, ModbusType::int16,
                                     LinearScaling{.scale = 0.1, .offset = 0.0}),
                                 tag("in_fault", ModbusArea::coil, 0, ModbusType::boolean)}};
}

// An io_context running on its own thread, as the gateway gives each driver (ADR-0010).
class DriverThread {
public:
    DriverThread() : thread_{[this] { io_.run(); }} {}
    DriverThread(const DriverThread&) = delete;
    DriverThread& operator=(const DriverThread&) = delete;
    DriverThread(DriverThread&&) = delete;
    DriverThread& operator=(DriverThread&&) = delete;
    ~DriverThread() {
        guard_.reset();
        thread_.join();
    }

    [[nodiscard]] asio::any_io_executor executor() { return io_.get_executor(); }

private:
    asio::io_context io_;
    asio::executor_work_guard<asio::io_context::executor_type> guard_{io_.get_executor()};
    std::thread thread_;
};

// Collects what the driver hands to its sink, from the driver's thread.
class Recorder {
public:
    [[nodiscard]] f2c::drivers::SampleSink sink() {
        return [this](std::span<const TagSample> samples) {
            const std::scoped_lock lock{mutex_};
            if (closed_) {
                ++late_calls_;
            }
            samples_.insert(samples_.end(), samples.begin(), samples.end());
        };
    }

    // Distinct tag names seen with the given quality.
    [[nodiscard]] std::size_t tags_with(Quality quality) const {
        const std::scoped_lock lock{mutex_};
        std::set<std::string> names;
        for (const auto& sample : samples_) {
            if (sample.quality == quality) {
                names.insert(sample.path.str());
            }
        }
        return names.size();
    }

    [[nodiscard]] std::vector<TagSample> samples() const {
        const std::scoped_lock lock{mutex_};
        return samples_;
    }

    // Any sink call from now on is a contract violation.
    void close() {
        const std::scoped_lock lock{mutex_};
        closed_ = true;
    }

    [[nodiscard]] int late_calls() const {
        const std::scoped_lock lock{mutex_};
        return late_calls_;
    }

private:
    mutable std::mutex mutex_;
    std::vector<TagSample> samples_;
    bool closed_{false};
    int late_calls_{0};
};

} // namespace

TEST_CASE("LibmodbusClient reads the simulator's registers and bits", "[modbus][component]") {
    const Simulator simulator;
    const auto client = make_libmodbus_client(tcp_config(simulator.port()));

    REQUIRE(client->connect());
    CHECK(client->connected());

    const auto holding = client->read_registers(ModbusArea::holding, 0, 5);
    REQUIRE(holding);
    CHECK(*holding == std::vector<std::uint16_t>{0, 0, 0, 120, 1});

    const auto input = client->read_registers(ModbusArea::input, 0, 1);
    REQUIRE(input);
    REQUIRE(input->size() == 1);
    CHECK((*input)[0] >= 230); // ~25 °C idle, in 0.1 °C
    CHECK((*input)[0] <= 270);

    const auto coil = client->read_bits(ModbusArea::coil, 0, 1);
    REQUIRE(coil);
    CHECK(*coil == std::vector<bool>{false});
}

TEST_CASE("LibmodbusClient resolves a host name", "[modbus][component]") {
    const Simulator simulator;
    auto config = tcp_config(simulator.port());
    config.host = "localhost";
    const auto client = make_libmodbus_client(config);

    REQUIRE(client->connect());
    CHECK(client->read_registers(ModbusArea::holding, 3, 1));
}

TEST_CASE("LibmodbusClient maps a Modbus exception to protocol_error and stays connected",
          "[modbus][component]") {
    const Simulator simulator;
    const auto client = make_libmodbus_client(tcp_config(simulator.port()));
    REQUIRE(client->connect());

    const auto outside = client->read_registers(ModbusArea::holding, 100, 1);

    REQUIRE_FALSE(outside);
    CHECK(outside.error().code == Errc::protocol_error);
    CHECK(outside.error().message.find("holding 100") != std::string::npos);
    CHECK(client->connected());
    CHECK(client->read_registers(ModbusArea::holding, 0, 1));
}

TEST_CASE("LibmodbusClient reports a refused connection as io_error", "[modbus][component]") {
    const auto client = make_libmodbus_client(tcp_config(closed_port()));

    const auto connected = client->connect();

    REQUIRE_FALSE(connected);
    CHECK(connected.error().code == Errc::io_error);
    CHECK(connected.error().message.find("127.0.0.1") != std::string::npos);
    CHECK_FALSE(client->connected());
}

TEST_CASE("LibmodbusClient times out on a device that does not answer", "[modbus][component]") {
    const SilentSocket silent;
    const auto client = make_libmodbus_client(tcp_config(silent.port(), 100ms));
    REQUIRE(client->connect());

    const auto start = std::chrono::steady_clock::now();
    const auto read = client->read_registers(ModbusArea::holding, 0, 1);
    const auto elapsed = std::chrono::steady_clock::now() - start;

    REQUIRE_FALSE(read);
    CHECK(read.error().code == Errc::timeout);
    CHECK(elapsed >= 90ms);
    CHECK(elapsed < 1s);
}

TEST_CASE("LibmodbusClient reports a lost connection", "[modbus][component]") {
    Simulator simulator;
    const auto client = make_libmodbus_client(tcp_config(simulator.port(), 200ms));
    REQUIRE(client->connect());

    simulator.stop();
    const auto lost = client->read_registers(ModbusArea::holding, 0, 1);

    REQUIRE_FALSE(lost);
    CHECK((lost.error().code == Errc::io_error || lost.error().code == Errc::timeout));
    client->close();
    CHECK_FALSE(client->connected());
}

TEST_CASE("ModbusDriver hands good samples from the simulator to the sink", "[modbus][component]") {
    const Simulator simulator;
    DriverThread thread;
    Recorder recorder;
    ModbusDriver driver{thread.executor(), filler(simulator.port()),
                        make_libmodbus_client(tcp_config(simulator.port()))};
    CHECK(driver.id() == "filler");
    CHECK(driver.state() == DriverState::stopped);

    driver.start(recorder.sink());

    REQUIRE(wait_until([&] { return recorder.tags_with(Quality::good) == 6; }, deadline));
    CHECK(driver.state() == DriverState::running);
    const auto samples = recorder.samples();
    const auto setpoint = std::ranges::find_if(
        samples, [](const TagSample& s) { return s.path.str().ends_with("/speed_setpoint"); });
    REQUIRE(setpoint != samples.end());
    CHECK(setpoint->value == f2c::core::Value{std::int64_t{120}});
    const auto temperature = std::ranges::find_if(
        samples, [](const TagSample& s) { return s.path.str().ends_with("/temperature"); });
    REQUIRE(temperature != samples.end());
    CHECK(std::holds_alternative<double>(temperature->value));

    driver.stop();
    recorder.close();

    CHECK(driver.state() == DriverState::stopped);
    // Several scan periods: any sink call now would break the stop() contract.
    CHECK_FALSE(wait_until([&] { return recorder.late_calls() > 0; }, 200ms));
}

TEST_CASE("ModbusDriver reports every tag bad while the device is unreachable",
          "[modbus][component]") {
    const auto port = closed_port();
    DriverThread thread;
    Recorder recorder;
    ModbusDriver driver{thread.executor(), filler(port), make_libmodbus_client(tcp_config(port))};

    driver.start(recorder.sink());

    REQUIRE(wait_until([&] { return recorder.tags_with(Quality::bad) == 6; }, deadline));
    CHECK(recorder.tags_with(Quality::good) == 0);
    CHECK(driver.state() == DriverState::faulted);
    driver.stop();
}

TEST_CASE("ModbusDriver stop is idempotent and allowed before start", "[modbus][component]") {
    DriverThread thread;
    const auto port = closed_port();

    SECTION("before start") {
        ModbusDriver driver{thread.executor(), filler(port),
                            make_libmodbus_client(tcp_config(port))};
        driver.stop();
        CHECK(driver.state() == DriverState::stopped);
    }
    SECTION("twice after start") {
        Recorder recorder;
        ModbusDriver driver{thread.executor(), filler(port),
                            make_libmodbus_client(tcp_config(port))};
        driver.start(recorder.sink());
        driver.stop();
        driver.stop();
        CHECK(driver.state() == DriverState::stopped);
    }
    SECTION("destroyed while running") {
        Recorder recorder;
        auto driver = std::make_unique<ModbusDriver>(thread.executor(), filler(port),
                                                     make_libmodbus_client(tcp_config(port)));
        driver->start(recorder.sink());
        driver.reset(); // the destructor stops it; ASan catches a handler outliving it
        CHECK(recorder.late_calls() == 0);
    }
}
