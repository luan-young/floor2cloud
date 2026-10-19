#include "f2c/config/plant_config.hpp"

#include <variant>

namespace f2c::config {

std::string_view driver_type(const DriverConfig& d) noexcept {
    // std::get_if instead of std::visit, which may throw bad_variant_access.
    if (std::get_if<ModbusTcpConfig>(&d) != nullptr) {
        return "modbus-tcp";
    }
    return "?"; // unreachable while DriverConfig has one alternative
}

} // namespace f2c::config
