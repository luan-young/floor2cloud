#include "f2c/config/plant_config.hpp"

#include <catch2/catch_test_macros.hpp>

using f2c::config::DriverConfig;
using f2c::config::ModbusTcpConfig;

TEST_CASE("driver_type names the driver of a DriverConfig", "[config]") {
    const DriverConfig driver = ModbusTcpConfig{};

    CHECK(f2c::config::driver_type(driver) == "modbus-tcp");
}
