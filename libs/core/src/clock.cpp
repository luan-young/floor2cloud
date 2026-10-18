#include "f2c/core/clock.hpp"

namespace f2c::core {

namespace {

class SystemClock final : public Clock {
public:
    [[nodiscard]] Timestamp now() const override {
        return std::chrono::time_point_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now());
    }

    [[nodiscard]] std::chrono::steady_clock::time_point steady_now() const override {
        return std::chrono::steady_clock::now();
    }
};

} // namespace

const Clock& system_clock() noexcept {
    static const SystemClock clock;
    return clock;
}

} // namespace f2c::core
