#include "f2c/drivers/modbus/modbus_driver.hpp"

#include <asio/post.hpp>
#include <spdlog/spdlog.h>

#include <cassert>
#include <chrono>
#include <exception>
#include <system_error>
#include <utility>
#include <variant>

namespace f2c::drivers::modbus {

namespace {

std::string endpoint_of(const config::DeviceConfig& device) {
    const auto& tcp = std::get<config::ModbusTcpConfig>(device.driver);
    return tcp.host + ":" + std::to_string(tcp.port);
}

} // namespace

ModbusDriver::ModbusDriver(const asio::any_io_executor& executor, config::DeviceConfig device,
                           std::unique_ptr<ModbusClient> client, const core::Clock& clock)
    : id_{device.id}, endpoint_{endpoint_of(device)}, client_{std::move(client)},
      poller_{std::move(device), *client_, clock}, strand_{asio::make_strand(executor)},
      timer_{strand_} {}

ModbusDriver::~ModbusDriver() {
    try {
        stop();
    } catch (...) {
        std::terminate(); // a pending handler would outlive the driver (ADR-0011)
    }
}

std::string_view ModbusDriver::id() const {
    return id_;
}

void ModbusDriver::start(SampleSink sink) {
    const bool already = started_.exchange(true);
    assert(!already && "ModbusDriver::start called twice");
    if (already) {
        return;
    }
    sink_ = std::move(sink);
    state_ = DriverState::connecting;
    pending_ = 1; // nothing runs on the strand before this post, which publishes it
    asio::post(strand_, [this] { on_wake(); });
}

void ModbusDriver::stop() {
    std::call_once(stop_once_, [this] {
        if (started_.load()) {
            auto stopped = stopped_.get_future();
            asio::post(strand_, [this] {
                stopping_ = true;
                timer_.cancel();
                client_->close();
                finish_stop_if_idle();
            });
            stopped.wait();
            spdlog::info("{}: stopped", id_);
        }
        state_ = DriverState::stopped;
    });
}

DriverState ModbusDriver::state() const {
    return state_.load();
}

void ModbusDriver::on_wake() {
    --pending_;
    if (stopping_) {
        finish_stop_if_idle();
        return;
    }
    step();
}

void ModbusDriver::step() {
    const bool was_connected = poller_.connected();
    auto result = poller_.poll();
    log_connection(was_connected);
    state_ = poller_.connected() ? DriverState::running : DriverState::faulted;
    if (!result.samples.empty()) {
        sink_(result.samples);
    }

    ++pending_;
    timer_.expires_at(result.next_wakeup);
    timer_.async_wait([this](const std::error_code& /*ec*/) { on_wake(); });
}

void ModbusDriver::finish_stop_if_idle() {
    if (pending_ == 0) {
        stopped_.set_value();
    }
}

void ModbusDriver::log_connection(bool was_connected) {
    const bool connected = poller_.connected();
    if (connected && !was_connected) {
        spdlog::info("{}: {} {}", id_, ever_connected_ ? "reconnected to" : "connected to",
                     endpoint_);
        ever_connected_ = true;
        reported_down_ = false;
        return;
    }
    if (connected) {
        return;
    }
    const auto& error = poller_.connection_error();
    const auto reason = error ? error->message : std::string{"unknown error"};
    if (!reported_down_) {
        spdlog::warn("{}: {}: {}; retrying", id_,
                     was_connected ? "connection lost" : "cannot connect", reason);
        reported_down_ = true;
    } else {
        spdlog::debug("{}: reconnect failed: {}", id_, reason);
    }
}

core::Expected<void> register_modbus_tcp(DriverRegistry& registry) {
    return registry.add(
        "modbus-tcp",
        [](const asio::any_io_executor& executor,
           const config::DeviceConfig& device) -> core::Expected<std::unique_ptr<Driver>> {
            const auto* tcp = std::get_if<config::ModbusTcpConfig>(&device.driver);
            if (tcp == nullptr) {
                return core::make_error(core::Errc::invalid_argument,
                                        device.id + ": not a modbus-tcp device");
            }
            return std::make_unique<ModbusDriver>(executor, device, make_libmodbus_client(*tcp));
        });
}

} // namespace f2c::drivers::modbus
