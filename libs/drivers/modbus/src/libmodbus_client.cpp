#include "f2c/drivers/modbus/modbus_client.hpp"

#include <modbus.h>

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace f2c::drivers::modbus {

namespace {

using config::ModbusArea;

struct ContextDeleter {
    void operator()(modbus_t* ctx) const noexcept {
        modbus_close(ctx);
        modbus_free(ctx);
    }
};
using Context = std::unique_ptr<modbus_t, ContextDeleter>;

std::string_view area_name(ModbusArea area) noexcept {
    switch (area) {
    case ModbusArea::coil:
        return "coil";
    case ModbusArea::discrete_input:
        return "discrete_input";
    case ModbusArea::holding:
        return "holding";
    case ModbusArea::input:
        break;
    }
    return "input";
}

// Exception responses leave the connection usable; a timeout is reported as such; anything else
// (refused, reset, malformed response) means the connection must be closed and reopened.
core::Errc errc_from(int err) noexcept {
    if (err >= EMBXILFUN && err <= EMBXGTAR) {
        return core::Errc::protocol_error;
    }
    if (err == ETIMEDOUT) {
        return core::Errc::timeout;
    }
    return core::Errc::io_error;
}

class LibmodbusClient final : public ModbusClient {
public:
    explicit LibmodbusClient(config::ModbusTcpConfig cfg)
        : cfg_{std::move(cfg)}, endpoint_{cfg_.host + ":" + std::to_string(cfg_.port)} {}

    core::Expected<void> connect() override {
        close();
        // The _pi variant resolves host names (Compose service names), unlike modbus_new_tcp.
        Context ctx{modbus_new_tcp_pi(cfg_.host.c_str(), std::to_string(cfg_.port).c_str())};
        if (!ctx) {
            return failure("connect " + endpoint_);
        }
        if (modbus_set_slave(ctx.get(), cfg_.unit_id) == -1) {
            return core::make_error(core::Errc::invalid_argument, "connect " + endpoint_ +
                                                                      ": invalid unit id " +
                                                                      std::to_string(cfg_.unit_id));
        }
        const auto timeout = std::chrono::duration_cast<std::chrono::microseconds>(cfg_.timeout);
        const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(timeout);
        modbus_set_response_timeout(ctx.get(), static_cast<std::uint32_t>(seconds.count()),
                                    static_cast<std::uint32_t>((timeout - seconds).count()));
        if (modbus_connect(ctx.get()) == -1) {
            return failure("connect " + endpoint_);
        }
        ctx_ = std::move(ctx);
        return {};
    }

    void close() noexcept override { ctx_.reset(); }

    [[nodiscard]] bool connected() const noexcept override { return ctx_ != nullptr; }

    core::Expected<std::vector<bool>> read_bits(ModbusArea area, std::uint16_t address,
                                                std::uint16_t count) override {
        if (!ctx_) {
            return not_connected();
        }
        std::vector<std::uint8_t> raw(count);
        const int read = area == ModbusArea::coil
                             ? modbus_read_bits(ctx_.get(), address, count, raw.data())
                             : modbus_read_input_bits(ctx_.get(), address, count, raw.data());
        if (read == -1) {
            return failure(describe(area, address, count));
        }
        if (read != count) {
            return short_response(area, address, count, read);
        }
        std::vector<bool> bits(count);
        for (std::size_t i = 0; i < raw.size(); ++i) {
            bits[i] = raw[i] != 0;
        }
        return bits;
    }

    core::Expected<std::vector<std::uint16_t>>
    read_registers(ModbusArea area, std::uint16_t address, std::uint16_t count) override {
        if (!ctx_) {
            return not_connected();
        }
        std::vector<std::uint16_t> registers(count);
        const int read =
            area == ModbusArea::holding
                ? modbus_read_registers(ctx_.get(), address, count, registers.data())
                : modbus_read_input_registers(ctx_.get(), address, count, registers.data());
        if (read == -1) {
            return failure(describe(area, address, count));
        }
        if (read != count) {
            return short_response(area, address, count, read);
        }
        return registers;
    }

private:
    [[nodiscard]] std::string describe(ModbusArea area, std::uint16_t address,
                                       std::uint16_t count) const {
        return "read " + std::string{area_name(area)} + " " + std::to_string(address) + "+" +
               std::to_string(count) + " from " + endpoint_;
    }

    // Reads errno right away: libmodbus reports every failure through it.
    static tl::unexpected<core::Error> failure(const std::string& what) {
        const int err = errno;
        return core::make_error(errc_from(err), what + ": " + modbus_strerror(err));
    }

    [[nodiscard]] tl::unexpected<core::Error> not_connected() const {
        return core::make_error(core::Errc::io_error, endpoint_ + ": not connected");
    }

    // A response with fewer items than asked for: the stream can no longer be trusted.
    [[nodiscard]] tl::unexpected<core::Error> short_response(ModbusArea area, std::uint16_t address,
                                                             std::uint16_t count, int read) const {
        return core::make_error(core::Errc::io_error, describe(area, address, count) +
                                                          ": short response (" +
                                                          std::to_string(read) + " items)");
    }

    config::ModbusTcpConfig cfg_;
    std::string endpoint_;
    Context ctx_;
};

} // namespace

std::unique_ptr<ModbusClient> make_libmodbus_client(const config::ModbusTcpConfig& cfg) {
    return std::make_unique<LibmodbusClient>(cfg);
}

} // namespace f2c::drivers::modbus
