#pragma once

#include "f2c/config/plant_config.hpp"
#include "f2c/core/clock.hpp"
#include "f2c/core/error.hpp"
#include "f2c/drivers/driver.hpp"
#include "f2c/drivers/modbus/modbus_client.hpp"
#include "f2c/drivers/modbus/poller.hpp"
#include "f2c/drivers/registry.hpp"

#include <asio/any_io_executor.hpp>
#include <asio/steady_timer.hpp>
#include <asio/strand.hpp>

#include <atomic>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

namespace f2c::drivers::modbus {

// Thin shell: a steady_timer on the executor calls Poller::poll() and re-arms at next_wakeup.
// State: connecting until the first poll, then running while connected, faulted while not.
// The executor must keep running handlers until stop() returns (the gateway stops drivers before
// stopping their io_context); the destructor calls stop() and terminates if that fails, since a
// handler could otherwise outlive the driver.
class ModbusDriver final : public Driver {
public:
    ModbusDriver(const asio::any_io_executor& executor, config::DeviceConfig device,
                 std::unique_ptr<ModbusClient> client,
                 const core::Clock& clock = core::system_clock());
    ModbusDriver(const ModbusDriver&) = delete;
    ModbusDriver& operator=(const ModbusDriver&) = delete;
    ModbusDriver(ModbusDriver&&) = delete;
    ModbusDriver& operator=(ModbusDriver&&) = delete;
    ~ModbusDriver() override;

    [[nodiscard]] std::string_view id() const override;
    void start(SampleSink sink) override;
    void stop() override;
    [[nodiscard]] DriverState state() const override;

private:
    // Strand only, from here down to the end of the class.
    void on_wake();
    void step();
    void finish_stop_if_idle();
    void log_connection(bool was_connected);

    std::string id_;
    std::string endpoint_; // host:port, for logs
    std::unique_ptr<ModbusClient> client_;
    Poller poller_;
    asio::strand<asio::any_io_executor> strand_;
    asio::steady_timer timer_;
    SampleSink sink_;
    std::atomic<DriverState> state_{DriverState::stopped};
    std::atomic<bool> started_{false};
    std::once_flag stop_once_;
    std::promise<void> stopped_; // set on the strand once no handler can run any more

    int pending_{0}; // handlers posted or armed that still reference this
    bool stopping_{false};
    bool reported_down_{false}; // the current outage was logged at warn
    bool ever_connected_{false};
};

// Registers type "modbus-tcp" (factory uses make_libmodbus_client).
core::Expected<void> register_modbus_tcp(DriverRegistry& registry);

} // namespace f2c::drivers::modbus
