#include <catch2/catch_test_macros.hpp>

#include <chrono>

#include "filler_model.hpp"
#include "register_map.hpp"

using namespace std::chrono_literals;
using f2c::sim::apply_client_writes;
using f2c::sim::FillerModel;
using f2c::sim::FillerOutputs;
using f2c::sim::MachineState;
using f2c::sim::RegisterImage;
using f2c::sim::to_registers;

TEST_CASE("to_registers follows the register map", "[sim][registers]") {
    const FillerOutputs outputs{
        .counter = 0xAABB'CCDDU,
        .speed = 118,
        .speed_setpoint = 120,
        .state = MachineState::faulted,
        .temperature_x10 = 417,
        .in_fault = true,
    };

    const auto image = to_registers(outputs);

    CHECK(image.holding[0] == 0xAABB); // high word in the lower address
    CHECK(image.holding[1] == 0xCCDD);
    CHECK(image.holding[2] == 118);
    CHECK(image.holding[3] == 120);
    CHECK(image.holding[4] == 2);
    CHECK(image.input[0] == 417);
    CHECK(image.coils[0] == 1);
}

TEST_CASE("to_registers encodes a negative temperature as two's complement", "[sim][registers]") {
    const FillerOutputs outputs{.temperature_x10 = -15};

    CHECK(to_registers(outputs).input[0] == 0xFFF1);
}

TEST_CASE("apply_client_writes forwards a new speed setpoint", "[sim][registers]") {
    FillerModel model{1};
    const auto published = to_registers(model.outputs());
    auto written = published;
    written.holding[3] = 200;

    apply_client_writes(model, written, published);

    CHECK(model.outputs().speed_setpoint == 200);
}

TEST_CASE("apply_client_writes forwards the fault coil", "[sim][registers]") {
    FillerModel model{1};
    auto published = to_registers(model.outputs());
    auto written = published;

    written.coils[0] = 1;
    apply_client_writes(model, written, published);
    CHECK(model.outputs().in_fault);
    CHECK(model.outputs().state == MachineState::faulted);

    published = to_registers(model.outputs());
    written = published;
    written.coils[0] = 0;
    apply_client_writes(model, written, published);
    CHECK_FALSE(model.outputs().in_fault);
}

TEST_CASE("apply_client_writes ignores read-only registers", "[sim][registers]") {
    FillerModel model{1};
    model.step(5s);
    const auto before = model.outputs();
    const auto published = to_registers(before);
    auto written = published;
    written.holding[0] = 0x1234;
    written.holding[1] = 0x5678;
    written.holding[2] = 300;
    written.holding[4] = 0;
    written.input[0] = 999;

    apply_client_writes(model, written, published);

    const auto after = model.outputs();
    CHECK(after.counter == before.counter);
    CHECK(after.speed == before.speed);
    CHECK(after.state == before.state);
    CHECK(after.temperature_x10 == before.temperature_x10);
    // The next publish overwrites what the client wrote.
    CHECK(to_registers(after).holding == published.holding);
}

TEST_CASE("apply_client_writes leaves the model alone when nothing changed", "[sim][registers]") {
    FillerModel model{1};
    model.set_speed_setpoint(80);
    const auto published = to_registers(model.outputs());

    // A client rewriting the current value is not a change.
    apply_client_writes(model, published, published);

    CHECK(model.outputs().speed_setpoint == 80);
    CHECK_FALSE(model.outputs().in_fault);
}

TEST_CASE("apply_client_writes clamps an out-of-range setpoint", "[sim][registers]") {
    FillerModel model{1};
    const auto published = to_registers(model.outputs());
    auto written = published;
    written.holding[3] = 1000;

    apply_client_writes(model, written, published);

    CHECK(to_registers(model.outputs()).holding[3] == 300);
}

TEST_CASE("RegisterImage sizes match the register map", "[sim][registers]") {
    const RegisterImage image{};

    CHECK(image.holding.size() == 5);
    CHECK(image.input.size() == 1);
    CHECK(image.coils.size() == 1);
}
