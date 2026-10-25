#include "f2c/drivers/registry.hpp"

#include <utility>

namespace f2c::drivers {

core::Expected<void> DriverRegistry::add(std::string type, DriverFactory factory) {
    if (contains(type)) {
        return core::make_error(core::Errc::invalid_argument,
                                "driver type '" + type + "' is already registered");
    }
    factories_.emplace(std::move(type), std::move(factory));
    return {};
}

core::Expected<std::unique_ptr<Driver>>
DriverRegistry::create(asio::any_io_executor executor, const config::DeviceConfig& device) const {
    const auto type = config::driver_type(device.driver);
    const auto it = factories_.find(type);
    if (it == factories_.end()) {
        return core::make_error(core::Errc::not_found, device.id +
                                                           ": no driver registered for type '" +
                                                           std::string{type} + "'");
    }
    return it->second(std::move(executor), device);
}

bool DriverRegistry::contains(std::string_view type) const {
    return factories_.contains(type);
}

} // namespace f2c::drivers
