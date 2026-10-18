#include "f2c/core/backoff.hpp"

#include <algorithm>
#include <cassert>

namespace f2c::core {

Backoff::Backoff(BackoffPolicy policy) : policy_(policy), current_(policy.initial) {
    assert(policy.initial.count() >= 0);
    assert(policy.initial <= policy.max);
    assert(policy.multiplier >= 1.0);
}

std::chrono::milliseconds Backoff::next_delay() {
    const auto delay = current_;
    if (current_ < policy_.max) {
        using Rep = std::chrono::milliseconds::rep;
        const double scaled = static_cast<double>(current_.count()) * policy_.multiplier;
        const double capped = std::min(scaled, static_cast<double>(policy_.max.count()));
        current_ = std::chrono::milliseconds{static_cast<Rep>(capped)};
    }
    return delay;
}

void Backoff::reset() noexcept {
    current_ = policy_.initial;
}

} // namespace f2c::core
