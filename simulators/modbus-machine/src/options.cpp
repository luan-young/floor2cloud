#include "options.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <string>
#include <system_error>
#include <utility>

namespace f2c::sim {

namespace {

constexpr std::array<std::string_view, 7> log_levels{"trace", "debug",    "info", "warn",
                                                     "error", "critical", "off"};

core::Expected<std::uint64_t> parse_number(std::string_view option, std::string_view text,
                                           std::uint64_t min, std::uint64_t max) {
    std::uint64_t value = 0;
    const auto* end = text.data() + text.size();
    const auto [ptr, ec] = std::from_chars(text.data(), end, value);
    if (ec != std::errc{} || ptr != end || value < min || value > max) {
        return core::make_error(core::Errc::invalid_argument,
                                std::string{option} + ": expected an integer in " +
                                    std::to_string(min) + ".." + std::to_string(max) + ", got '" +
                                    std::string{text} + "'");
    }
    return value;
}

core::Expected<std::string> parse_non_empty(std::string_view option, std::string_view text) {
    if (text.empty()) {
        return core::make_error(core::Errc::invalid_argument,
                                std::string{option} + ": must not be empty");
    }
    return std::string{text};
}

core::Expected<void> apply(SimulatorOptions& options, std::string_view option,
                           std::string_view value) {
    if (option == "--bind") {
        return parse_non_empty(option, value).map([&](std::string text) {
            options.bind_address = std::move(text);
        });
    }
    if (option == "--port") {
        return parse_number(option, value, 1, 65'535).map([&](std::uint64_t port) {
            options.port = static_cast<std::uint16_t>(port);
        });
    }
    if (option == "--step-ms") {
        return parse_number(option, value, 1, 60'000).map([&](std::uint64_t ms) {
            options.step_period = std::chrono::milliseconds{ms};
        });
    }
    if (option == "--seed") {
        return parse_number(option, value, 0, 0xFFFF'FFFFU).map([&](std::uint64_t seed) {
            options.seed = static_cast<std::uint32_t>(seed);
        });
    }
    if (option == "--health-file") {
        return parse_non_empty(option, value).map([&](std::string path) {
            options.health_file = std::move(path);
        });
    }
    if (option == "--log-level") {
        if (std::ranges::find(log_levels, value) == log_levels.end()) {
            return core::make_error(core::Errc::invalid_argument,
                                    "--log-level: unknown level '" + std::string{value} + "'");
        }
        options.log_level = std::string{value};
        return {};
    }
    return core::make_error(core::Errc::invalid_argument,
                            "unknown option '" + std::string{option} + "'");
}

} // namespace

core::Expected<SimulatorOptions> parse_options(std::span<const std::string_view> args) {
    SimulatorOptions options;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string_view arg = args[i];
        if (arg == "--help" || arg == "-h") {
            options.help = true;
            continue;
        }
        if (!arg.starts_with("--")) {
            return core::make_error(core::Errc::invalid_argument,
                                    "unexpected argument '" + std::string{arg} + "'");
        }

        std::string_view option = arg;
        std::string_view value;
        if (const auto equals = arg.find('='); equals != std::string_view::npos) {
            option = arg.substr(0, equals);
            value = arg.substr(equals + 1);
        } else if (i + 1 < args.size()) {
            value = args[++i];
        } else {
            return core::make_error(core::Errc::invalid_argument,
                                    std::string{option} + ": missing value");
        }

        if (auto applied = apply(options, option, value); !applied) {
            return tl::unexpected(std::move(applied.error()));
        }
    }
    return options;
}

std::string_view usage() {
    return "Usage: f2c-modbus-machine [options]\n"
           "\n"
           "Simulated filling machine served over Modbus TCP (unit id 1).\n"
           "\n"
           "Options:\n"
           "  --bind <address>      address to listen on (default 0.0.0.0)\n"
           "  --port <port>         TCP port (default 502)\n"
           "  --step-ms <ms>        model step period (default 100)\n"
           "  --seed <n>            random seed for the temperature noise (default: random)\n"
           "  --health-file <path>  touch this file every 5 s while running\n"
           "  --log-level <level>   trace, debug, info, warn, error, critical, off (default info)\n"
           "  -h, --help            show this help\n";
}

} // namespace f2c::sim
