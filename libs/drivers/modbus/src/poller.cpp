#include "f2c/drivers/modbus/poller.hpp"

#include "f2c/drivers/modbus/decode.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <span>
#include <utility>
#include <variant>

namespace f2c::drivers::modbus {

namespace {

using config::ModbusAddress;
using config::ModbusArea;

bool is_bit_area(ModbusArea area) noexcept {
    return area == ModbusArea::coil || area == ModbusArea::discrete_input;
}

// io_error and timeout mean the connection is unusable; anything else is the device answering.
bool connection_lost(const core::Error& error) noexcept {
    return error.code == core::Errc::io_error || error.code == core::Errc::timeout;
}

} // namespace

Poller::Poller(config::DeviceConfig device, ModbusClient& client, const core::Clock& clock)
    : device_{std::move(device)}, client_{&client}, clock_{&clock},
      groups_{plan_scan_groups(device_.tags)}, due_(groups_.size()),
      last_values_(device_.tags.size()),
      backoff_{std::get<config::ModbusTcpConfig>(device_.driver).reconnect} {}

Poller::Result Poller::poll() {
    if (!client_->connected()) {
        if (clock_->steady_now() < retry_at_) {
            return {.samples = {}, .next_wakeup = retry_at_};
        }
        if (!client_->connect()) {
            return fail_connection();
        }
        backoff_.reset();
        outage_reported_ = false;
        std::ranges::fill(due_, clock_->steady_now());
    }

    std::vector<core::TagSample> samples;
    for (std::size_t g = 0; g < groups_.size(); ++g) {
        if (clock_->steady_now() < due_[g]) {
            continue;
        }
        for (const auto& block : groups_[g].blocks) {
            if (!read_block(block, samples)) {
                return fail_connection();
            }
        }
        const auto period = groups_[g].period;
        due_[g] += period;
        if (const auto now = clock_->steady_now(); due_[g] <= now) {
            due_[g] = now + period; // fell behind: skip the missed scans
        }
    }
    return {.samples = std::move(samples), .next_wakeup = std::ranges::min(due_)};
}

bool Poller::connected() const noexcept {
    return client_->connected();
}

bool Poller::read_block(const ReadBlock& block, std::vector<core::TagSample>& samples) {
    const auto emit_bad = [&] {
        const auto now = clock_->now();
        for (const auto i : block.tags) {
            samples.push_back(sample(i, last_values_[i], core::Quality::bad, now));
        }
    };

    if (is_bit_area(block.area)) {
        auto bits = client_->read_bits(block.area, block.start, block.count);
        if (!bits) {
            if (connection_lost(bits.error())) {
                return false;
            }
            emit_bad();
            return true;
        }
        assert(bits->size() == block.count);
        const auto now = clock_->now();
        for (const auto i : block.tags) {
            const auto& address = std::get<ModbusAddress>(device_.tags[i].address);
            last_values_[i] = decode_bit((*bits)[address.address - block.start]);
            samples.push_back(sample(i, last_values_[i], core::Quality::good, now));
        }
        return true;
    }

    auto registers = client_->read_registers(block.area, block.start, block.count);
    if (!registers) {
        if (connection_lost(registers.error())) {
            return false;
        }
        emit_bad();
        return true;
    }
    assert(registers->size() == block.count);
    const auto now = clock_->now();
    const std::span<const std::uint16_t> words{*registers};
    for (const auto i : block.tags) {
        const auto& tag = device_.tags[i];
        const auto& address = std::get<ModbusAddress>(tag.address);
        auto value =
            decode_registers(address, words.subspan(address.address - block.start), tag.scaling);
        if (!value) {
            samples.push_back(sample(i, last_values_[i], core::Quality::bad, now));
            continue;
        }
        last_values_[i] = *std::move(value);
        samples.push_back(sample(i, last_values_[i], core::Quality::good, now));
    }
    return true;
}

Poller::Result Poller::fail_connection() {
    if (client_->connected()) {
        client_->close();
    }
    std::vector<core::TagSample> samples;
    if (!outage_reported_) {
        outage_reported_ = true;
        const auto now = clock_->now();
        samples.reserve(device_.tags.size());
        for (std::size_t i = 0; i < device_.tags.size(); ++i) {
            samples.push_back(sample(i, last_values_[i], core::Quality::bad, now));
        }
    }
    retry_at_ = clock_->steady_now() + backoff_.next_delay();
    return {.samples = std::move(samples), .next_wakeup = retry_at_};
}

core::TagSample Poller::sample(std::size_t tag, core::Value value, core::Quality quality,
                               core::Timestamp time) const {
    return core::TagSample{.path = device_.tags[tag].path,
                           .value = std::move(value),
                           .quality = quality,
                           .source_time = time,
                           .gateway_time = time};
}

} // namespace f2c::drivers::modbus
