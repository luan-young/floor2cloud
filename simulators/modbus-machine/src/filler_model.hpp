#pragma once

#include <chrono>
#include <cstdint>
#include <random>

namespace f2c::sim {

enum class MachineState : std::uint8_t { stopped = 0, running = 1, faulted = 2 };

struct FillerOutputs {
    std::uint32_t counter{};
    std::uint16_t speed{};          // bottles/min
    std::uint16_t speed_setpoint{}; // bottles/min
    MachineState state{MachineState::stopped};
    std::int16_t temperature_x10{}; // 0.1 °C
    bool in_fault{};
};

// A filling machine (spec § 2.1.1). Pure: no I/O, no clock; time advances only through step().
class FillerModel {
public:
    static constexpr std::uint16_t default_setpoint = 120; // bottles/min
    static constexpr std::uint16_t max_setpoint = 300;     // bottles/min

    explicit FillerModel(std::uint32_t seed);

    void set_speed_setpoint(std::uint16_t bpm); // clamped to 0..300
    void set_fault(bool forced);                // true: speed drops to 0 at once, counter stops
    void set_counter(std::uint32_t counter);    // start from a given count (e.g. to test the wrap)
    void step(std::chrono::milliseconds dt);
    [[nodiscard]] FillerOutputs outputs() const;

private:
    std::mt19937 rng_;
    std::normal_distribution<double> noise_;
    double speed_{0.0}; // bottles/min
    std::uint16_t setpoint_{default_setpoint};
    bool in_fault_{false};
    std::uint32_t counter_{0};
    double partial_bottle_{0.0}; // fraction of the bottle being filled, 0..1
    double temperature_{0.0};    // °C, without noise
    double temperature_noise_{0.0};
};

} // namespace f2c::sim
