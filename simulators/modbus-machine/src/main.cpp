// f2c-modbus-machine: a simulated filling machine served over Modbus TCP (spec § 2.1).
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <atomic>
#include <csignal>
#include <cstdint>
#include <iostream>
#include <random>
#include <string_view>
#include <vector>

#include "options.hpp"
#include "server.hpp"

namespace {

constexpr int exit_ok = 0;
constexpr int exit_config_error = 1;
constexpr int exit_runtime_error = 2;

// Lock-free, so it is safe to set from a signal handler.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
std::atomic<bool> stop_requested{false};
static_assert(std::atomic<bool>::is_always_lock_free);

extern "C" void handle_stop_signal(int /*signal*/) {
    stop_requested.store(true);
}

void install_signal_handlers() {
    struct sigaction action {};
    action.sa_handler = handle_stop_signal; // no SA_RESTART: select() returns EINTR at once
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, nullptr);
    sigaction(SIGTERM, &action, nullptr);
}

} // namespace

int main(int argc, char* argv[]) {
    const std::vector<std::string_view> args(argv + 1, argv + argc);
    const auto options = f2c::sim::parse_options(args);
    if (!options) {
        std::cerr << "f2c-modbus-machine: " << options.error().message << "\n\n"
                  << f2c::sim::usage();
        return exit_config_error;
    }
    if (options->help) {
        std::cout << f2c::sim::usage();
        return exit_ok;
    }

    spdlog::set_default_logger(spdlog::stdout_color_mt("modbus-machine"));
    spdlog::set_pattern("%Y-%m-%dT%H:%M:%S.%e %^%l%$ [%n] %v");
    spdlog::set_level(spdlog::level::from_str(options->log_level));

    install_signal_handlers();

    const std::uint32_t seed = options->seed.value_or(std::random_device{}());
    if (const auto result = f2c::sim::run_server(*options, seed, stop_requested); !result) {
        spdlog::critical("{}", result.error().message);
        return exit_runtime_error;
    }
    return exit_ok;
}
