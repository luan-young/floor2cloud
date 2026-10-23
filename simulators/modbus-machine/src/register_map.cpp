#include "register_map.hpp"

#include <cstddef>

namespace f2c::sim {

namespace {

constexpr std::size_t counter_high = 0;
constexpr std::size_t counter_low = 1;
constexpr std::size_t speed = 2;
constexpr std::size_t speed_setpoint = 3;
constexpr std::size_t state = 4;
constexpr std::size_t temperature = 0;
constexpr std::size_t in_fault = 0;

} // namespace

RegisterImage to_registers(const FillerOutputs& outputs) {
    RegisterImage image;
    image.holding[counter_high] = static_cast<std::uint16_t>(outputs.counter >> 16U);
    image.holding[counter_low] = static_cast<std::uint16_t>(outputs.counter & 0xFFFFU);
    image.holding[speed] = outputs.speed;
    image.holding[speed_setpoint] = outputs.speed_setpoint;
    image.holding[state] = static_cast<std::uint16_t>(outputs.state);
    image.input[temperature] = static_cast<std::uint16_t>(outputs.temperature_x10);
    image.coils[in_fault] = outputs.in_fault ? 1 : 0;
    return image;
}

void apply_client_writes(FillerModel& model, const RegisterImage& current,
                         const RegisterImage& published) {
    if (current.holding[speed_setpoint] != published.holding[speed_setpoint]) {
        model.set_speed_setpoint(current.holding[speed_setpoint]);
    }
    if (current.coils[in_fault] != published.coils[in_fault]) {
        model.set_fault(current.coils[in_fault] != 0);
    }
}

} // namespace f2c::sim
