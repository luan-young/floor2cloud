#include "f2c/config/loader.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <chrono>
#include <string>
#include <variant>

#include "test_documents.hpp"

using namespace std::chrono_literals;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::StartsWith;
using f2c::config::ByteOrder;
using f2c::config::ConfigError;
using f2c::config::ModbusAddress;
using f2c::config::ModbusArea;
using f2c::config::ModbusTcpConfig;
using f2c::config::ModbusType;
using f2c::config::TagConfig;
using f2c::config::WordOrder;
using f2c::config::test::errors_of;
using f2c::config::test::must_parse;
using f2c::config::test::scaling_of;
using f2c::config::test::yaml;

namespace {

const ModbusAddress& modbus(const TagConfig& tag) {
    const auto* address = std::get_if<ModbusAddress>(&tag.address);
    REQUIRE(address != nullptr);
    return *address;
}

void check_address(const TagConfig& tag, ModbusArea area, std::uint16_t address, ModbusType type) {
    CAPTURE(tag.name);
    const auto& a = modbus(tag);
    CHECK(a.area == area);
    CHECK(a.address == address);
    CHECK(a.type == type);
    CHECK(a.byte_order == ByteOrder::big);
    CHECK(a.word_order == WordOrder::big);
}

const ConfigError* find_error(const std::vector<ConfigError>& errors, std::string_view key_path) {
    const auto it = std::ranges::find(errors, key_path, &ConfigError::key_path);
    return it == errors.end() ? nullptr : &*it;
}

} // namespace

TEST_CASE("The example plant file loads into the expected PlantConfig", "[config][loader]") {
    auto result = f2c::config::load_plant_config(F2C_PLANT1_YAML);
    if (!result) {
        FAIL(f2c::config::format_errors(result.error()));
    }
    const auto& plant = result->plant;

    CHECK(result->warnings.empty());
    CHECK(plant.site == "plant1");
    CHECK(plant.mqtt.uri == "tcp://mosquitto:1883");
    CHECK(plant.mqtt.client_id == "gateway-01");
    CHECK(plant.mqtt.qos == 1);
    CHECK(plant.mqtt.keepalive == 10s);
    CHECK(plant.mqtt.heartbeat == 30s);

    REQUIRE(plant.devices.size() == 1);
    const auto& filler = plant.devices[0];
    CHECK(filler.id == "filler");
    CHECK(filler.path.str() == "plant1/production/line1/cell-c/filler");

    const auto* driver = std::get_if<ModbusTcpConfig>(&filler.driver);
    REQUIRE(driver != nullptr);
    CHECK(driver->host == "modbus-machine");
    CHECK(driver->port == 502);
    CHECK(driver->unit_id == 1);
    CHECK(driver->timeout == 500ms);
    CHECK(driver->reconnect.initial == 500ms);
    CHECK(driver->reconnect.max == 10'000ms);
    CHECK(driver->reconnect.multiplier == 2.0);
    CHECK(driver->byte_order == ByteOrder::big);
    CHECK(driver->word_order == WordOrder::big);

    const auto& tags = filler.tags;
    REQUIRE(tags.size() == 6);
    CHECK(tags[0].name == "counter");
    CHECK(tags[4].name == "temperature");
    CHECK(tags[4].path.str() == "plant1/production/line1/cell-c/filler/temperature");
    for (const auto& tag : tags) {
        CHECK(tag.scan_period == 1000ms);
    }
    check_address(tags[0], ModbusArea::holding, 0, ModbusType::uint32);
    check_address(tags[1], ModbusArea::holding, 2, ModbusType::uint16);
    check_address(tags[2], ModbusArea::holding, 3, ModbusType::uint16);
    check_address(tags[3], ModbusArea::holding, 4, ModbusType::uint16);
    check_address(tags[4], ModbusArea::input, 0, ModbusType::int16);
    check_address(tags[5], ModbusArea::coil, 0, ModbusType::boolean);

    CHECK(scaling_of(tags[4]).scale == 0.1);
    CHECK(scaling_of(tags[4]).offset == 0.0);
    CHECK_FALSE(tags[0].scaling.has_value());
    CHECK_FALSE(tags[5].scaling.has_value());
}

TEST_CASE("Optional keys get their defaults", "[config][loader]") {
    const auto loaded = must_parse(yaml({}));
    const auto& plant = loaded.plant;

    CHECK(plant.mqtt.qos == 1);
    CHECK(plant.mqtt.keepalive == 10s);
    CHECK(plant.mqtt.heartbeat == 30s);
    const auto& device = plant.devices.at(0);
    const auto& driver = std::get<ModbusTcpConfig>(device.driver);
    CHECK(driver.port == 502);
    CHECK(driver.unit_id == 1);
    CHECK(driver.timeout == 500ms);
    CHECK(driver.reconnect.initial == 500ms);
    CHECK(driver.reconnect.max == 10'000ms);
    CHECK(driver.byte_order == ByteOrder::big);
    CHECK(driver.word_order == WordOrder::big);
    CHECK(device.tags.at(0).scan_period == 1000ms);
    CHECK_FALSE(device.tags.at(0).scaling.has_value());
}

TEST_CASE("Tags inherit device settings unless they override them", "[config][loader]") {
    const auto loaded = must_parse(yaml({
        .device = {"scan_ms: 250"},
        .driver = {"byte_order: little", "word_order: little"},
        .tags =
            {
                "{ name: inherits, area: holding, address: 0, type: uint32 }",
                "{ name: overrides, area: holding, address: 2, type: float32, scan_ms: 5000, "
                "byte_order: big, word_order: big }",
                "{ name: offset_only, area: input, address: 0, type: int16, offset: -40 }",
            },
    }));
    const auto& tags = loaded.plant.devices.at(0).tags;
    REQUIRE(tags.size() == 3);

    CHECK(tags[0].scan_period == 250ms);
    CHECK(modbus(tags[0]).byte_order == ByteOrder::little);
    CHECK(modbus(tags[0]).word_order == WordOrder::little);

    CHECK(tags[1].scan_period == 5000ms);
    CHECK(modbus(tags[1]).byte_order == ByteOrder::big);
    CHECK(modbus(tags[1]).word_order == WordOrder::big);

    CHECK(scaling_of(tags[2]).scale == 1.0);
    CHECK(scaling_of(tags[2]).offset == -40.0);
}

TEST_CASE("Several devices with the same tag names load", "[config][loader]") {
    const auto loaded = must_parse(R"(
site: plant1
mqtt: { uri: "tcp://broker:1883", client_id: gw }
devices:
  - id: a
    path: area/a
    driver: { type: modbus-tcp, host: a }
    tags: [ { name: t, area: coil, address: 0, type: bool } ]
  - id: b
    path: area/b
    driver: { type: modbus-tcp, host: b }
    tags: [ { name: t, area: coil, address: 0, type: bool } ]
)");

    REQUIRE(loaded.plant.devices.size() == 2);
    CHECK(loaded.plant.devices[1].tags.at(0).path.str() == "plant1/area/b/t");
}

TEST_CASE("Every error in a file is reported with its position and key path", "[config][loader]") {
    const auto errors = errors_of(R"(site: Plant1
mqtt:
  uri: tcp://broker:1883
  client_id: gw
  qos: 3
devices:
  - id: d1
    path: area/d1
    scan_mss: 100
    driver:
      type: modbus-tcp
      host: h
      port: abc
    tags:
      - { name: t, area: holdng, address: 0, type: uint16 }
)");

    CHECK(errors.size() == 5);
    const auto check = [&](std::string_view key_path, std::string_view location,
                           std::string_view message) {
        CAPTURE(key_path);
        const auto* error = find_error(errors, key_path);
        REQUIRE(error != nullptr);
        CHECK(error->location == location);
        CHECK_THAT(error->message, ContainsSubstring(std::string{message}));
    };
    check("site", "test.yaml:1:7", "Plant1");
    check("mqtt.qos", "test.yaml:5:8", "between 0 and 2");
    check("devices[0].scan_mss", "test.yaml:9:5", "did you mean 'scan_ms'?");
    check("devices[0].driver.port", "test.yaml:13:13", "expected an integer");
    check("devices[0].tags[0].area", "test.yaml:15:26",
          "unknown area 'holdng'; expected one of: coil, discrete_input, holding, input");
}

TEST_CASE("Errors in several devices and tags are all reported", "[config][loader]") {
    const auto errors = errors_of(R"(
site: plant1
mqtt: { uri: "tcp://broker:1883", client_id: gw }
devices:
  - id: a
    path: area/a
    driver: { type: modbus-tcp, host: a, port: 0 }
    tags:
      - { name: t1, area: coil, address: 0, type: uint16 }
      - { name: t2, area: coil, address: 0, type: bool, scale: 2 }
  - id: b
    path: area/b
    driver: { type: modbus-tcp }
    tags:
      - { name: t1, area: holding, address: 70000, type: uint16 }
)");

    CHECK(errors.size() == 5);
    CHECK(find_error(errors, "devices[0].driver.port") != nullptr);
    CHECK(find_error(errors, "devices[0].tags[0].type") != nullptr);
    CHECK(find_error(errors, "devices[0].tags[1].scale") != nullptr);
    CHECK(find_error(errors, "devices[1].driver.host") != nullptr);
    CHECK(find_error(errors, "devices[1].tags[0].address") != nullptr);
}

TEST_CASE("A missing file is an error, not an exception", "[config][loader]") {
    const auto result = f2c::config::load_plant_config("does/not/exist.yaml");

    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().size() == 1);
    const auto& error = result.error()[0];
    CHECK(error.location == "does/not/exist.yaml");
    CHECK(error.key_path.empty());
    CHECK_THAT(error.message, ContainsSubstring("not found"));
}

TEST_CASE("A directory is not a plant file", "[config][loader]") {
    const auto result = f2c::config::load_plant_config(".");

    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().size() == 1);
    CHECK_THAT(result.error()[0].message, ContainsSubstring("not a regular file"));
}

TEST_CASE("A YAML syntax error is an error with its position", "[config][loader]") {
    const auto errors = errors_of("site: plant1\nmqtt: { uri: tcp://x:1\n");

    REQUIRE(errors.size() == 1);
    CHECK_THAT(errors[0].location, StartsWith("test.yaml:"));
    CHECK(errors[0].key_path.empty());
    CHECK_THAT(errors[0].message, StartsWith("YAML syntax error"));
}

TEST_CASE("An empty document or a non-mapping root is an error", "[config][loader]") {
    const auto text = GENERATE(as<std::string>{}, "", "# only a comment\n", "- a\n- b\n", "42\n");
    CAPTURE(text);

    const auto errors = errors_of(text);

    REQUIRE(errors.size() == 1);
    CHECK(errors[0].key_path.empty());
    CHECK_THAT(errors[0].message, ContainsSubstring("mapping"));
}

TEST_CASE("A timeout not shorter than the smallest scan period is a warning", "[config][loader]") {
    const auto loaded = must_parse(yaml({
        .driver = {"timeout_ms: 1000"},
        .tags =
            {
                "{ name: slow, area: holding, address: 0, type: uint16 }",
                "{ name: fast, area: holding, address: 1, type: uint16, scan_ms: 1000 }",
            },
    }));

    REQUIRE(loaded.warnings.size() == 1);
    const auto& warning = loaded.warnings[0];
    CHECK(warning.location == "test.yaml:11:19");
    CHECK(warning.key_path == "devices[0].driver.timeout_ms");
    CHECK_THAT(warning.message, ContainsSubstring("1000 ms"));
}
