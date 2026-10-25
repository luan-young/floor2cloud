#pragma once

#include "f2c/config/plant_config.hpp"
#include "f2c/core/error.hpp"

#include <cstdint>
#include <vector>

namespace f2c::drivers::modbus {

// Blocking Modbus client. Not thread-safe: owned and used by one driver thread.
// Error codes: io_error / timeout = connection is unusable (caller closes and reconnects);
//              protocol_error = Modbus exception response (connection still fine).
class ModbusClient {
public:
    ModbusClient() = default;
    ModbusClient(const ModbusClient&) = delete;
    ModbusClient& operator=(const ModbusClient&) = delete;
    ModbusClient(ModbusClient&&) = delete;
    ModbusClient& operator=(ModbusClient&&) = delete;
    virtual ~ModbusClient() = default;

    virtual core::Expected<void> connect() = 0;
    virtual void close() noexcept = 0;
    [[nodiscard]] virtual bool connected() const noexcept = 0;

    // area: coil or discrete_input; count 1..2000
    virtual core::Expected<std::vector<bool>>
    read_bits(config::ModbusArea area, std::uint16_t address, std::uint16_t count) = 0;

    // area: holding or input; count 1..125; registers in host order as received
    virtual core::Expected<std::vector<std::uint16_t>>
    read_registers(config::ModbusArea area, std::uint16_t address, std::uint16_t count) = 0;
};

} // namespace f2c::drivers::modbus
