#include "f2c/config/loader.hpp"

#include <catch2/catch_test_macros.hpp>

#include <vector>

using f2c::config::ConfigError;
using f2c::config::format_errors;

TEST_CASE("format_errors writes one line per error", "[config][format]") {
    const std::vector<ConfigError> errors{
        {"plant.yaml:14:9", "devices[0].tags[2].type", "unknown type 'uint64'"},
        {"plant.yaml:3:3", "mqtt.qos", "must be between 0 and 2 (got 3)"},
    };

    CHECK(format_errors(errors) ==
          "plant.yaml:14:9: devices[0].tags[2].type: unknown type 'uint64'\n"
          "plant.yaml:3:3: mqtt.qos: must be between 0 and 2 (got 3)\n");
}

TEST_CASE("format_errors leaves out empty parts", "[config][format]") {
    const std::vector<ConfigError> errors{
        {"missing.yaml", "", "file not found"},
        {"", "devices", "at least one device is required"},
        {"", "", "something"},
    };

    CHECK(format_errors(errors) == "missing.yaml: file not found\n"
                                   "devices: at least one device is required\n"
                                   "something\n");
}

TEST_CASE("format_errors of no errors is empty", "[config][format]") {
    CHECK(format_errors({}).empty());
}
