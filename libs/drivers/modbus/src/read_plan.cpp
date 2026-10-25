#include "f2c/drivers/modbus/read_plan.hpp"

#include <algorithm>
#include <numeric>
#include <tuple>
#include <variant>

namespace f2c::drivers::modbus {

namespace {

using config::ModbusAddress;
using config::ModbusArea;

std::uint16_t max_per_read(ModbusArea area) noexcept {
    const bool bits = area == ModbusArea::coil || area == ModbusArea::discrete_input;
    return bits ? max_bits_per_read : max_registers_per_read;
}

} // namespace

std::uint16_t width(config::ModbusType type) noexcept {
    switch (type) {
    case config::ModbusType::int32:
    case config::ModbusType::uint32:
    case config::ModbusType::float32:
        return 2;
    case config::ModbusType::boolean:
    case config::ModbusType::int16:
    case config::ModbusType::uint16:
        break;
    }
    return 1;
}

std::vector<ScanGroup> plan_scan_groups(std::span<const config::TagConfig> tags) {
    // Visit tags by (period, area, address), ties in configuration order: then a single pass
    // either extends the last block or opens a new one.
    std::vector<std::size_t> order(tags.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::ranges::stable_sort(order, {}, [&tags](std::size_t i) {
        const auto& address = std::get<ModbusAddress>(tags[i].address);
        return std::tuple{tags[i].scan_period, address.area, address.address};
    });

    std::vector<ScanGroup> groups;
    for (const auto i : order) {
        const auto& tag = tags[i];
        const auto& address = std::get<ModbusAddress>(tag.address);
        if (groups.empty() || groups.back().period != tag.scan_period) {
            groups.push_back(ScanGroup{.period = tag.scan_period, .blocks = {}});
        }
        auto& blocks = groups.back().blocks;

        // Unsigned arithmetic: a tag may end at 65536, one past the last address.
        const unsigned tag_start = address.address;
        const unsigned tag_end = tag_start + width(address.type);
        if (!blocks.empty()) {
            auto& block = blocks.back();
            const unsigned block_end = block.start + block.count;
            const bool merges = block.area == address.area && tag_start <= block_end &&
                                tag_end - block.start <= max_per_read(address.area);
            if (merges) {
                block.count =
                    static_cast<std::uint16_t>(std::max(block_end, tag_end) - block.start);
                block.tags.push_back(i);
                continue;
            }
        }
        blocks.push_back(ReadBlock{.area = address.area,
                                   .start = address.address,
                                   .count = width(address.type),
                                   .tags = {i}});
    }
    return groups;
}

} // namespace f2c::drivers::modbus
