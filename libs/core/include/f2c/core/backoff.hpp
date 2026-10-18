#pragma once

#include <chrono>

namespace f2c::core {

struct BackoffPolicy {
    std::chrono::milliseconds initial{500};
    std::chrono::milliseconds max{10'000};
    double multiplier{2.0}; // >= 1.0
};

// Deterministic bounded exponential backoff (no jitter in M1: a single gateway, see ADR-0010).
class Backoff {
public:
    // Preconditions (asserted): 0 <= initial <= max, multiplier >= 1.0.
    explicit Backoff(BackoffPolicy policy);

    [[nodiscard]] std::chrono::milliseconds next_delay(); // initial, initial*m, …, capped at max
    void reset() noexcept;

private:
    BackoffPolicy policy_;
    std::chrono::milliseconds current_;
};

} // namespace f2c::core
