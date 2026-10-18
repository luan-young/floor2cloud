#include "f2c/core/testing/manual_clock.hpp"

#include <cassert>

namespace f2c::core::testing {

ManualClock::ManualClock(Timestamp start) : wall_(start) {}

Timestamp ManualClock::now() const {
    return wall_;
}

std::chrono::steady_clock::time_point ManualClock::steady_now() const {
    return steady_;
}

void ManualClock::advance(std::chrono::nanoseconds d) {
    assert(d.count() >= 0);
    wall_ += d;
    steady_ += std::chrono::duration_cast<std::chrono::steady_clock::duration>(d);
}

} // namespace f2c::core::testing
