#include "filler_model.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace f2c::sim {

namespace {

constexpr double ramp_rate = 10.0;          // bottles/min per second
constexpr double idle_temperature = 25.0;   // °C
constexpr double full_speed_heating = 20.0; // °C above idle at max_setpoint
constexpr double temperature_lag = 30.0;    // s, first-order time constant
constexpr double temperature_sigma = 0.2;   // °C

double target_temperature(double speed) {
    return idle_temperature + full_speed_heating * speed / FillerModel::max_setpoint;
}

} // namespace

FillerModel::FillerModel(std::uint32_t seed)
    : rng_{seed}, noise_{0.0, temperature_sigma}, temperature_{idle_temperature},
      temperature_noise_{noise_(rng_)} {}

void FillerModel::set_speed_setpoint(std::uint16_t bpm) {
    setpoint_ = std::min(bpm, max_setpoint);
}

void FillerModel::set_fault(bool forced) {
    in_fault_ = forced;
    if (forced) {
        speed_ = 0.0;
        partial_bottle_ = 0.0;
    }
}

void FillerModel::set_counter(std::uint32_t counter) {
    counter_ = counter;
}

void FillerModel::step(std::chrono::milliseconds dt) {
    const double seconds = std::chrono::duration<double>(dt).count();

    if (!in_fault_) {
        const double max_change = ramp_rate * seconds;
        speed_ += std::clamp(static_cast<double>(setpoint_) - speed_, -max_change, max_change);

        partial_bottle_ += speed_ / 60.0 * seconds;
        const double whole = std::floor(partial_bottle_);
        partial_bottle_ -= whole;
        // Unsigned arithmetic wraps at 2^32, as the register map specifies.
        counter_ += static_cast<std::uint32_t>(whole);
    }

    const double alpha = 1.0 - std::exp(-seconds / temperature_lag);
    temperature_ += (target_temperature(speed_) - temperature_) * alpha;
    temperature_noise_ = noise_(rng_);
}

FillerOutputs FillerModel::outputs() const {
    MachineState state = MachineState::running;
    if (in_fault_) {
        state = MachineState::faulted;
    } else if (setpoint_ == 0 && std::lround(speed_) == 0) {
        state = MachineState::stopped;
    }

    const double temperature_x10 = std::round((temperature_ + temperature_noise_) * 10.0);
    return FillerOutputs{
        .counter = counter_,
        .speed = static_cast<std::uint16_t>(std::lround(speed_)),
        .speed_setpoint = setpoint_,
        .state = state,
        .temperature_x10 = static_cast<std::int16_t>(
            std::clamp(temperature_x10, double{std::numeric_limits<std::int16_t>::min()},
                       double{std::numeric_limits<std::int16_t>::max()})),
        .in_fault = in_fault_,
    };
}

} // namespace f2c::sim
