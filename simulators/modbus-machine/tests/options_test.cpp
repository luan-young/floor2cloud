#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <chrono>
#include <string>
#include <string_view>
#include <vector>

#include "options.hpp"

using namespace std::chrono_literals;
using f2c::core::Errc;
using f2c::sim::parse_options;

namespace {

auto parse(std::vector<std::string_view> args) {
    return parse_options(args);
}

} // namespace

TEST_CASE("parse_options defaults", "[sim][options]") {
    const auto options = parse({});

    REQUIRE(options.has_value());
    CHECK(options->bind_address == "0.0.0.0");
    CHECK(options->port == 502);
    CHECK(options->step_period == 100ms);
    CHECK_FALSE(options->seed.has_value());
    CHECK_FALSE(options->health_file.has_value());
    CHECK(options->log_level == "info");
    CHECK_FALSE(options->help);
}

TEST_CASE("parse_options reads every option", "[sim][options]") {
    const auto options =
        parse({"--bind", "127.0.0.1", "--port", "1502", "--step-ms", "50", "--seed", "7",
               "--health-file", "/tmp/f2c-healthy", "--log-level", "debug"});

    REQUIRE(options.has_value());
    CHECK(options->bind_address == "127.0.0.1");
    CHECK(options->port == 1502);
    CHECK(options->step_period == 50ms);
    CHECK(options->seed == 7U);
    CHECK(options->health_file == "/tmp/f2c-healthy");
    CHECK(options->log_level == "debug");
}

TEST_CASE("parse_options accepts --opt=value", "[sim][options]") {
    const auto options = parse({"--port=1502", "--seed=4294967295"});

    REQUIRE(options.has_value());
    CHECK(options->port == 1502);
    CHECK(options->seed == 4'294'967'295U);
}

TEST_CASE("parse_options recognises help", "[sim][options]") {
    const auto flag = GENERATE(as<std::string_view>{}, "--help", "-h");

    const auto options = parse({flag});

    REQUIRE(options.has_value());
    CHECK(options->help);
}

TEST_CASE("parse_options rejects invalid input", "[sim][options]") {
    const auto args = GENERATE(
        std::vector<std::string_view>{"--port"}, std::vector<std::string_view>{"--port", "abc"},
        std::vector<std::string_view>{"--port", "0"},
        std::vector<std::string_view>{"--port", "65536"},
        std::vector<std::string_view>{"--port", "-1"},
        std::vector<std::string_view>{"--port", "12x"},
        std::vector<std::string_view>{"--step-ms", "0"},
        std::vector<std::string_view>{"--step-ms", "60001"},
        std::vector<std::string_view>{"--seed", "4294967296"},
        std::vector<std::string_view>{"--log-level", "verbose"},
        std::vector<std::string_view>{"--health-file", ""},
        std::vector<std::string_view>{"--bind", ""}, std::vector<std::string_view>{"--unknown"},
        std::vector<std::string_view>{"positional"});
    CAPTURE(args);

    const auto options = parse(args);

    REQUIRE_FALSE(options.has_value());
    CHECK(options.error().code == Errc::invalid_argument);
}

TEST_CASE("parse_options names the offending option", "[sim][options]") {
    const auto options = parse({"--port", "abc"});

    REQUIRE_FALSE(options.has_value());
    CHECK(options.error().message.find("--port") != std::string::npos);
}
