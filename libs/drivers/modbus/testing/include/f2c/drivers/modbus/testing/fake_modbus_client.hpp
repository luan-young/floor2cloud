#pragma once

#include "f2c/core/testing/manual_clock.hpp"
#include "f2c/drivers/modbus/modbus_client.hpp"

#include <chrono>
#include <cstdint>
#include <map>
#include <utility>
#include <vector>

namespace f2c::drivers::modbus::testing {

// In-memory Modbus device for tests: a register and bit map per area, failure injection and a
// log of every call. Unset addresses read as 0 / false.
class FakeModbusClient final : public ModbusClient {
public:
    struct Read {
        config::ModbusArea area;
        std::uint16_t address;
        std::uint16_t count;

        friend bool operator==(const Read&, const Read&) = default;
    };

    // When `clock` is given, a call that times out advances it by `timeout`, as a blocking call
    // to a silent device would.
    explicit FakeModbusClient(core::testing::ManualClock* clock = nullptr,
                              std::chrono::milliseconds timeout = std::chrono::milliseconds{500});

    core::Expected<void> connect() override;
    void close() noexcept override;
    [[nodiscard]] bool connected() const noexcept override;
    core::Expected<std::vector<bool>> read_bits(config::ModbusArea area, std::uint16_t address,
                                                std::uint16_t count) override;
    core::Expected<std::vector<std::uint16_t>>
    read_registers(config::ModbusArea area, std::uint16_t address, std::uint16_t count) override;

    void set_register(config::ModbusArea area, std::uint16_t address, std::uint16_t value);
    void set_bit(config::ModbusArea area, std::uint16_t address, bool value);

    // false: the device stops answering. connect() fails with io_error; reads on an open
    // connection fail with timeout.
    void set_responding(bool responding) noexcept { responding_ = responding; }

    // A read whose block starts at (area, address) fails with `code` (e.g. protocol_error for a
    // Modbus exception response). Ignored while the device is not responding.
    void fail_reads_at(config::ModbusArea area, std::uint16_t address, core::Errc code);
    void clear_read_failures() noexcept { read_failures_.clear(); }

    [[nodiscard]] int connect_calls() const noexcept { return connect_calls_; }
    [[nodiscard]] int close_calls() const noexcept { return close_calls_; }
    [[nodiscard]] const std::vector<Read>& reads() const noexcept { return reads_; }
    void clear_reads() noexcept { reads_.clear(); }

private:
    using Key = std::pair<config::ModbusArea, std::uint16_t>;

    core::Expected<void> check_read(const Read& read);

    core::testing::ManualClock* clock_;
    std::chrono::milliseconds timeout_;
    bool responding_{true};
    bool connected_{false};
    int connect_calls_{0};
    int close_calls_{0};
    std::map<Key, std::uint16_t> registers_;
    std::map<Key, bool> bits_;
    std::map<Key, core::Errc> read_failures_;
    std::vector<Read> reads_;
};

} // namespace f2c::drivers::modbus::testing
