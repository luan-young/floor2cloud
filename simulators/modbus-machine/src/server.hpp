#pragma once

#include "f2c/core/error.hpp"

#include <atomic>
#include <cstdint>
#include <functional>

#include "options.hpp"

namespace f2c::sim {

// Serves a FillerModel over Modbus TCP until `stop` becomes true (spec § 4.7): a single-threaded
// select() loop that accepts several concurrent clients, steps the model every
// `options.step_period`, forwards client writes to the model and touches the health file.
// `stop` is polled at least every 100 ms, so it may be set from a signal handler or another
// thread. `listening` is called once with the bound port (useful with `options.port == 0`, which
// picks an ephemeral port) before the first request is served.
// Returns an error only when it cannot start (bad address, port in use) or select() fails.
[[nodiscard]] core::Expected<void>
run_server(const SimulatorOptions& options, std::uint32_t seed, const std::atomic<bool>& stop,
           const std::function<void(std::uint16_t port)>& listening = {});

} // namespace f2c::sim
