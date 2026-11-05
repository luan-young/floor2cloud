#include "f2c/drivers/modbus/modbus_driver.hpp"

#include <asio/io_context.hpp>
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <utility>

using namespace std::chrono_literals;
using f2c::config::DeviceConfig;
using f2c::config::ModbusTcpConfig;
using f2c::core::Errc;
using f2c::drivers::DriverRegistry;
using f2c::drivers::DriverState;
using f2c::drivers::modbus::register_modbus_tcp;

namespace {

DeviceConfig modbus_device() {
    auto path = f2c::core::TagPath::parse("plant1/production/line1/cell-c/filler");
    REQUIRE(path);
    return DeviceConfig{.id = "filler",
                        .path = *std::move(path),
                        .driver = ModbusTcpConfig{.host = "modbus-machine"},
                        .tags = {}};
}

} // namespace

TEST_CASE("register_modbus_tcp registers the modbus-tcp type", "[modbus]") {
    DriverRegistry registry;

    REQUIRE(register_modbus_tcp(registry));

    CHECK(registry.contains("modbus-tcp"));
}

TEST_CASE("The modbus-tcp factory builds a stopped driver without any I/O", "[modbus]") {
    asio::io_context io; // never run: creating the driver must not need it
    DriverRegistry registry;
    REQUIRE(register_modbus_tcp(registry));

    auto driver = registry.create(io.get_executor(), modbus_device());

    REQUIRE(driver);
    CHECK((*driver)->id() == "filler");
    CHECK((*driver)->state() == DriverState::stopped);
}

TEST_CASE("register_modbus_tcp twice is an error", "[modbus]") {
    DriverRegistry registry;
    REQUIRE(register_modbus_tcp(registry));

    auto again = register_modbus_tcp(registry);

    REQUIRE_FALSE(again);
    CHECK(again.error().code == Errc::invalid_argument);
}
