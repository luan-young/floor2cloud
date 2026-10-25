#pragma once

#include "f2c/core/tag_sample.hpp"

#include <cstdint>
#include <functional>
#include <span>
#include <string_view>

namespace f2c::drivers {

// Called on the driver's thread. Must be thread-safe and must not block (it should copy and post).
using SampleSink = std::function<void(std::span<const core::TagSample>)>;

enum class DriverState : std::uint8_t { stopped, connecting, running, faulted };

// One device speaking one protocol (ADR-0007). Drivers produce normalized samples and know
// nothing about publishing.
class Driver {
public:
    Driver() = default;
    Driver(const Driver&) = delete;
    Driver& operator=(const Driver&) = delete;
    Driver(Driver&&) = delete;
    Driver& operator=(Driver&&) = delete;
    virtual ~Driver() = default;

    [[nodiscard]] virtual std::string_view id() const = 0;

    // Non-blocking. Work runs on the executor given at construction. Call at most once.
    virtual void start(SampleSink sink) = 0;

    // Idempotent. Blocks until in-flight I/O finishes (bounded by the protocol timeout);
    // after it returns the sink is never called again.
    // Precondition: not called from the driver's own executor.
    virtual void stop() = 0;

    [[nodiscard]] virtual DriverState state() const = 0; // thread-safe
};

} // namespace f2c::drivers
