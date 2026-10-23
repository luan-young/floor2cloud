#pragma once

#include <array>
#include <cstdint>

#include "filler_model.hpp"

namespace f2c::sim {

// The Modbus tables of the simulator (spec § 2.1.1), as served to clients. Register values are in
// host order; libmodbus sends them big-endian.
struct RegisterImage {
    std::array<std::uint16_t, 5> holding{}; // counter hi, counter lo, speed, speed_setpoint, state
    std::array<std::uint16_t, 1> input{};   // temperature_x10
    std::array<std::uint8_t, 1> coils{};    // in_fault
};

[[nodiscard]] RegisterImage to_registers(const FillerOutputs& outputs);

// Forwards client writes to the model: the writable entries (holding 3, coil 0) of `current` that
// differ from `published`, the image last given to clients. Other entries are read-only and are
// overwritten by the next to_registers().
void apply_client_writes(FillerModel& model, const RegisterImage& current,
                         const RegisterImage& published);

} // namespace f2c::sim
