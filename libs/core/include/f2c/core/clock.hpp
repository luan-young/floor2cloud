#pragma once

#include "f2c/core/timestamp.hpp"

#include <chrono>

namespace f2c::core {

// Injected wherever code needs "now", so tests can control time (NF-5).
class Clock {
public:
    Clock() = default;
    Clock(const Clock&) = delete;
    Clock& operator=(const Clock&) = delete;
    Clock(Clock&&) = delete;
    Clock& operator=(Clock&&) = delete;
    virtual ~Clock() = default;

    [[nodiscard]] virtual Timestamp now() const = 0; // wall clock, for timestamps
    [[nodiscard]] virtual std::chrono::steady_clock::time_point
    steady_now() const = 0; // scheduling
};

// The process-wide real clock: std::chrono::system_clock and std::chrono::steady_clock.
[[nodiscard]] const Clock& system_clock() noexcept;

} // namespace f2c::core
