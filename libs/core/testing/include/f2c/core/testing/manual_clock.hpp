#pragma once

#include "f2c/core/clock.hpp"
#include "f2c/core/timestamp.hpp"

#include <chrono>

namespace f2c::core::testing {

// A Clock that only moves when told to. Not thread-safe: tests advance it from the thread that
// reads it.
class ManualClock final : public Clock {
public:
    explicit ManualClock(Timestamp start = Timestamp{});

    [[nodiscard]] Timestamp now() const override;
    [[nodiscard]] std::chrono::steady_clock::time_point steady_now() const override;

    void advance(std::chrono::nanoseconds d); // moves both clocks; d >= 0

private:
    Timestamp wall_;
    std::chrono::steady_clock::time_point steady_;
};

} // namespace f2c::core::testing
