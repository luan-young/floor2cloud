#include "f2c/drivers/modbus/testing/fake_modbus_client.hpp"

#include <cassert>
#include <string>

namespace f2c::drivers::modbus::testing {

FakeModbusClient::FakeModbusClient(core::testing::ManualClock* clock,
                                   std::chrono::milliseconds timeout)
    : clock_{clock}, timeout_{timeout} {}

core::Expected<void> FakeModbusClient::connect() {
    ++connect_calls_;
    if (!responding_) {
        return core::make_error(core::Errc::io_error, "connection refused");
    }
    connected_ = true;
    return {};
}

void FakeModbusClient::close() noexcept {
    ++close_calls_;
    connected_ = false;
}

bool FakeModbusClient::connected() const noexcept {
    return connected_;
}

core::Expected<std::vector<bool>>
FakeModbusClient::read_bits(config::ModbusArea area, std::uint16_t address, std::uint16_t count) {
    assert(area == config::ModbusArea::coil || area == config::ModbusArea::discrete_input);
    if (auto ok = check_read({.area = area, .address = address, .count = count}); !ok) {
        return tl::unexpected(std::move(ok).error());
    }
    std::vector<bool> bits(count);
    for (std::uint16_t i = 0; i < count; ++i) {
        const auto it = bits_.find({area, static_cast<std::uint16_t>(address + i)});
        bits[i] = it != bits_.end() && it->second;
    }
    return bits;
}

core::Expected<std::vector<std::uint16_t>> FakeModbusClient::read_registers(config::ModbusArea area,
                                                                            std::uint16_t address,
                                                                            std::uint16_t count) {
    assert(area == config::ModbusArea::holding || area == config::ModbusArea::input);
    if (auto ok = check_read({.area = area, .address = address, .count = count}); !ok) {
        return tl::unexpected(std::move(ok).error());
    }
    std::vector<std::uint16_t> registers(count);
    for (std::uint16_t i = 0; i < count; ++i) {
        const auto it = registers_.find({area, static_cast<std::uint16_t>(address + i)});
        registers[i] = it != registers_.end() ? it->second : std::uint16_t{0};
    }
    return registers;
}

void FakeModbusClient::set_register(config::ModbusArea area, std::uint16_t address,
                                    std::uint16_t value) {
    registers_[{area, address}] = value;
}

void FakeModbusClient::set_bit(config::ModbusArea area, std::uint16_t address, bool value) {
    bits_[{area, address}] = value;
}

void FakeModbusClient::fail_reads_at(config::ModbusArea area, std::uint16_t address,
                                     core::Errc code) {
    read_failures_[{area, address}] = code;
}

core::Expected<void> FakeModbusClient::check_read(const Read& read) {
    reads_.push_back(read);
    if (!connected_) {
        return core::make_error(core::Errc::io_error, "not connected");
    }
    if (!responding_) {
        if (clock_ != nullptr) {
            clock_->advance(timeout_);
        }
        return core::make_error(core::Errc::timeout, "no response");
    }
    if (const auto it = read_failures_.find({read.area, read.address});
        it != read_failures_.end()) {
        return core::make_error(it->second, "injected failure at " + std::to_string(read.address));
    }
    return {};
}

} // namespace f2c::drivers::modbus::testing
