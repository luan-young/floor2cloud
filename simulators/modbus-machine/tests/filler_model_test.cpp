#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <vector>

#include "filler_model.hpp"

using namespace std::chrono_literals;
using f2c::sim::FillerModel;
using f2c::sim::MachineState;

namespace {

constexpr std::uint32_t seed = 42;

// Steps the model in 100 ms increments, like the server loop does.
void run_for(FillerModel& model, std::chrono::milliseconds duration) {
    for (auto elapsed = 0ms; elapsed < duration; elapsed += 100ms) {
        model.step(100ms);
    }
}

} // namespace

TEST_CASE("FillerModel starts running toward 120 bpm", "[sim][filler]") {
    const FillerModel model{seed};

    const auto out = model.outputs();

    CHECK(out.counter == 0);
    CHECK(out.speed == 0);
    CHECK(out.speed_setpoint == 120);
    CHECK(out.state == MachineState::running);
    CHECK_FALSE(out.in_fault);
}

TEST_CASE("FillerModel ramps speed at 10 bpm/s", "[sim][filler]") {
    FillerModel model{seed};

    run_for(model, 1s);
    CHECK(model.outputs().speed == 10);

    run_for(model, 5s);
    CHECK(model.outputs().speed == 60);

    run_for(model, 10s);
    CHECK(model.outputs().speed == 120); // reached the setpoint, no overshoot
}

TEST_CASE("FillerModel ramps down toward a lower setpoint", "[sim][filler]") {
    FillerModel model{seed};
    run_for(model, 12s);
    REQUIRE(model.outputs().speed == 120);

    model.set_speed_setpoint(100);
    run_for(model, 1s);
    CHECK(model.outputs().speed == 110);

    run_for(model, 5s);
    CHECK(model.outputs().speed == 100);
}

TEST_CASE("FillerModel ramp does not depend on the step size", "[sim][filler]") {
    FillerModel model{seed};

    model.step(3s);

    CHECK(model.outputs().speed == 30);
}

TEST_CASE("FillerModel counter integrates speed", "[sim][filler]") {
    FillerModel model{seed};
    run_for(model, 12s);
    REQUIRE(model.outputs().speed == 120);
    const auto start = model.outputs().counter;

    run_for(model, 60s);

    const auto filled = model.outputs().counter - start;
    CHECK(filled >= 119);
    CHECK(filled <= 121);
}

TEST_CASE("FillerModel counter wraps at 2^32", "[sim][filler]") {
    FillerModel model{seed};
    model.set_counter(0xFFFF'FFFFU);
    run_for(model, 12s); // ramp-up alone fills 12 bottles

    CHECK(model.outputs().counter < 100);
}

TEST_CASE("FillerModel with setpoint 0 stops", "[sim][filler]") {
    FillerModel model{seed};
    run_for(model, 12s);

    model.set_speed_setpoint(0);
    CHECK(model.outputs().state == MachineState::running); // still ramping down

    run_for(model, 12s);
    const auto stopped = model.outputs();
    CHECK(stopped.speed == 0);
    CHECK(stopped.state == MachineState::stopped);

    run_for(model, 10s);
    CHECK(model.outputs().counter == stopped.counter);
}

TEST_CASE("FillerModel fault stops the machine immediately", "[sim][filler]") {
    FillerModel model{seed};
    run_for(model, 12s);

    model.set_fault(true);
    const auto faulted = model.outputs();
    CHECK(faulted.in_fault);
    CHECK(faulted.speed == 0);
    CHECK(faulted.state == MachineState::faulted);
    CHECK(faulted.speed_setpoint == 120); // the setpoint is kept

    run_for(model, 10s);
    CHECK(model.outputs().counter == faulted.counter);
    CHECK(model.outputs().speed == 0);
    CHECK(model.outputs().state == MachineState::faulted);
}

TEST_CASE("FillerModel resumes after the fault is cleared", "[sim][filler]") {
    FillerModel model{seed};
    run_for(model, 12s);
    model.set_fault(true);
    run_for(model, 5s);
    const auto counter_at_clear = model.outputs().counter;

    model.set_fault(false);
    CHECK_FALSE(model.outputs().in_fault);
    CHECK(model.outputs().state == MachineState::running);

    run_for(model, 2s);
    CHECK(model.outputs().speed == 20); // ramps up again from standstill
    run_for(model, 30s);
    CHECK(model.outputs().speed == 120);
    CHECK(model.outputs().counter > counter_at_clear);
}

TEST_CASE("FillerModel clamps the setpoint to 300", "[sim][filler]") {
    FillerModel model{seed};

    model.set_speed_setpoint(301);
    CHECK(model.outputs().speed_setpoint == 300);

    model.set_speed_setpoint(65535);
    CHECK(model.outputs().speed_setpoint == 300);

    model.set_speed_setpoint(300);
    CHECK(model.outputs().speed_setpoint == 300);
}

TEST_CASE("FillerModel temperature follows speed within plausible bounds", "[sim][filler]") {
    FillerModel model{seed};

    // Idle at the start: ~25 °C.
    CHECK(model.outputs().temperature_x10 >= 230);
    CHECK(model.outputs().temperature_x10 <= 270);

    // Full speed for a long time: approaches ~45 °C, never runs away.
    model.set_speed_setpoint(300);
    std::int16_t lowest = model.outputs().temperature_x10;
    std::int16_t highest = lowest;
    for (int i = 0; i < 6'000; ++i) { // 10 min
        model.step(100ms);
        const auto temperature = model.outputs().temperature_x10;
        lowest = std::min(lowest, temperature);
        highest = std::max(highest, temperature);
    }
    CHECK(lowest >= 230);
    CHECK(highest <= 470);
    CHECK(model.outputs().temperature_x10 >= 430);

    // Stopped for a long time: cools back to ~25 °C.
    model.set_speed_setpoint(0);
    run_for(model, 600s);
    CHECK(model.outputs().temperature_x10 >= 230);
    CHECK(model.outputs().temperature_x10 <= 270);
}

TEST_CASE("FillerModel temperature has noise", "[sim][filler]") {
    FillerModel model{seed};
    run_for(model, 600s); // steady state at 120 bpm

    std::vector<std::int16_t> readings;
    for (int i = 0; i < 50; ++i) {
        model.step(100ms);
        readings.push_back(model.outputs().temperature_x10);
    }

    const auto [lowest, highest] = std::minmax_element(readings.begin(), readings.end());
    CHECK(*lowest != *highest);
    CHECK(*highest - *lowest <= 20); // σ = 0.2 °C: a 2 °C spread in 50 samples would be absurd
}

TEST_CASE("FillerModel with the same seed reproduces the same sequence", "[sim][filler]") {
    FillerModel first{seed};
    FillerModel second{seed};
    FillerModel other{seed + 1};

    bool differs_from_other = false;
    for (int i = 0; i < 200; ++i) {
        first.step(100ms);
        second.step(100ms);
        other.step(100ms);
        CHECK(first.outputs().temperature_x10 == second.outputs().temperature_x10);
        differs_from_other = differs_from_other ||
                             first.outputs().temperature_x10 != other.outputs().temperature_x10;
    }
    CHECK(differs_from_other);
}
