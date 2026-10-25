#include "f2c/drivers/registry.hpp"

#include <asio/io_context.hpp>
#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <string_view>
#include <utility>

using f2c::config::DeviceConfig;
using f2c::config::ModbusTcpConfig;
using f2c::core::Errc;
using f2c::core::Expected;
using f2c::drivers::Driver;
using f2c::drivers::DriverRegistry;
using f2c::drivers::DriverState;
using f2c::drivers::SampleSink;

namespace {

class FakeDriver final : public Driver {
public:
    FakeDriver(asio::any_io_executor executor, std::string id)
        : executor_{std::move(executor)}, id_{std::move(id)} {}

    [[nodiscard]] std::string_view id() const override { return id_; }
    void start(SampleSink /*sink*/) override { state_ = DriverState::running; }
    void stop() override { state_ = DriverState::stopped; }
    [[nodiscard]] DriverState state() const override { return state_; }

    [[nodiscard]] const asio::any_io_executor& executor() const { return executor_; }

private:
    asio::any_io_executor executor_;
    std::string id_;
    DriverState state_{DriverState::stopped};
};

Expected<std::unique_ptr<Driver>> make_fake(asio::any_io_executor executor,
                                            const DeviceConfig& device) {
    return std::make_unique<FakeDriver>(std::move(executor), device.id);
}

DeviceConfig modbus_device(std::string id) {
    auto path = f2c::core::TagPath::parse("plant1/packaging/line1/cell-c/filler");
    REQUIRE(path);
    return DeviceConfig{.id = std::move(id),
                        .path = *std::move(path),
                        .driver = ModbusTcpConfig{.host = "localhost"},
                        .tags = {}};
}

} // namespace

TEST_CASE("DriverRegistry creates a driver of a registered type", "[drivers]") {
    asio::io_context io;
    DriverRegistry registry;
    REQUIRE(registry.add("modbus-tcp", make_fake));

    auto driver = registry.create(io.get_executor(), modbus_device("filler"));

    REQUIRE(driver);
    REQUIRE(*driver != nullptr);
    CHECK((*driver)->id() == "filler");
    CHECK((*driver)->state() == DriverState::stopped);
    const auto* fake = dynamic_cast<const FakeDriver*>(driver->get());
    REQUIRE(fake != nullptr);
    CHECK(fake->executor() == asio::any_io_executor{io.get_executor()});
}

TEST_CASE("DriverRegistry passes the device configuration to the factory", "[drivers]") {
    asio::io_context io;
    DriverRegistry registry;
    std::string seen_host;
    REQUIRE(registry.add("modbus-tcp",
                         [&seen_host](asio::any_io_executor executor, const DeviceConfig& device)
                             -> Expected<std::unique_ptr<Driver>> {
                             seen_host = std::get<ModbusTcpConfig>(device.driver).host;
                             return make_fake(std::move(executor), device);
                         }));

    REQUIRE(registry.create(io.get_executor(), modbus_device("filler")));

    CHECK(seen_host == "localhost");
}

TEST_CASE("DriverRegistry reports an unknown driver type as not_found", "[drivers]") {
    asio::io_context io;
    const DriverRegistry registry;

    auto driver = registry.create(io.get_executor(), modbus_device("filler"));

    REQUIRE_FALSE(driver);
    CHECK(driver.error().code == Errc::not_found);
    CHECK(driver.error().message.find("modbus-tcp") != std::string::npos);
    CHECK(driver.error().message.find("filler") != std::string::npos);
}

TEST_CASE("DriverRegistry rejects a duplicate type", "[drivers]") {
    DriverRegistry registry;
    REQUIRE(registry.add("modbus-tcp", make_fake));

    auto again = registry.add("modbus-tcp", make_fake);

    REQUIRE_FALSE(again);
    CHECK(again.error().code == Errc::invalid_argument);
    CHECK(again.error().message.find("modbus-tcp") != std::string::npos);
}

TEST_CASE("DriverRegistry propagates a factory error unchanged", "[drivers]") {
    asio::io_context io;
    DriverRegistry registry;
    REQUIRE(registry.add("modbus-tcp",
                         [](const asio::any_io_executor& /*executor*/,
                            const DeviceConfig& /*device*/) -> Expected<std::unique_ptr<Driver>> {
                             return f2c::core::make_error(Errc::io_error, "no socket");
                         }));

    auto driver = registry.create(io.get_executor(), modbus_device("filler"));

    REQUIRE_FALSE(driver);
    CHECK(driver.error().code == Errc::io_error);
    CHECK(driver.error().message == "no socket");
}

TEST_CASE("DriverRegistry::contains reports registered types", "[drivers]") {
    DriverRegistry registry;
    CHECK_FALSE(registry.contains("modbus-tcp"));

    REQUIRE(registry.add("modbus-tcp", make_fake));

    CHECK(registry.contains("modbus-tcp"));
    CHECK_FALSE(registry.contains("opc-ua"));
}
