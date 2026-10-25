#pragma once

#include "f2c/config/plant_config.hpp"
#include "f2c/core/error.hpp"
#include "f2c/drivers/driver.hpp"

#include <asio/any_io_executor.hpp>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>

namespace f2c::drivers {

using DriverFactory = std::function<core::Expected<std::unique_ptr<Driver>>(
    asio::any_io_executor, const config::DeviceConfig&)>;

// An instance, not global state: services register what they link explicitly (ADR-0007).
class DriverRegistry {
public:
    // Error (invalid_argument) if the type is already registered.
    core::Expected<void> add(std::string type, DriverFactory factory);

    // Error (not_found) if no factory matches config::driver_type(device.driver).
    [[nodiscard]] core::Expected<std::unique_ptr<Driver>>
    create(asio::any_io_executor executor, const config::DeviceConfig& device) const;

    [[nodiscard]] bool contains(std::string_view type) const;

private:
    std::map<std::string, DriverFactory, std::less<>> factories_;
};

} // namespace f2c::drivers
