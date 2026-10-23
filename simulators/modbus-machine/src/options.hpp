#pragma once

#include "f2c/core/error.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace f2c::sim {

struct SimulatorOptions {
    std::string bind_address{"0.0.0.0"};
    std::uint16_t port{502}; // 0 (library use only): an ephemeral port
    std::chrono::milliseconds step_period{100};
    std::optional<std::uint32_t> seed;                // none: random, logged at startup
    std::optional<std::filesystem::path> health_file; // touched every 5 s while the loop runs
    std::string log_level{"info"};                    // spdlog level name
    bool help{false};
};

// Parses the command line (without argv[0]). Accepts `--opt value` and `--opt=value`.
[[nodiscard]] core::Expected<SimulatorOptions>
parse_options(std::span<const std::string_view> args);

[[nodiscard]] std::string_view usage();

} // namespace f2c::sim
