#include "f2c/config/loader.hpp"
#include "f2c/drivers/modbus/read_plan.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

using namespace std::chrono_literals;
using f2c::config::ByteOrder;
using f2c::config::ModbusAddress;
using f2c::config::ModbusArea;
using f2c::config::ModbusType;
using f2c::config::TagConfig;
using f2c::config::WordOrder;
using f2c::drivers::modbus::max_bits_per_read;
using f2c::drivers::modbus::max_registers_per_read;
using f2c::drivers::modbus::plan_scan_groups;
using f2c::drivers::modbus::ReadBlock;
using f2c::drivers::modbus::width;

namespace {

TagConfig tag(ModbusArea area, std::uint16_t address, ModbusType type,
              std::chrono::milliseconds period = 1000ms) {
    static int counter = 0;
    const std::string name = "t" + std::to_string(counter++);
    auto path = f2c::core::TagPath::parse("plant1/dev/" + name);
    REQUIRE(path);
    return TagConfig{.name = name,
                     .path = *std::move(path),
                     .scan_period = period,
                     .scaling = std::nullopt,
                     .address = ModbusAddress{.area = area,
                                              .address = address,
                                              .type = type,
                                              .byte_order = ByteOrder::big,
                                              .word_order = WordOrder::big}};
}

TagConfig holding(std::uint16_t address, ModbusType type = ModbusType::uint16,
                  std::chrono::milliseconds period = 1000ms) {
    return tag(ModbusArea::holding, address, type, period);
}

TagConfig coil(std::uint16_t address, std::chrono::milliseconds period = 1000ms) {
    return tag(ModbusArea::coil, address, ModbusType::boolean, period);
}

void check_block(const ReadBlock& block, ModbusArea area, std::uint16_t start, std::uint16_t count,
                 const std::vector<std::size_t>& tags) {
    CHECK(block.area == area);
    CHECK(block.start == start);
    CHECK(block.count == count);
    CHECK(block.tags == tags);
}

} // namespace

TEST_CASE("width counts registers per type", "[modbus]") {
    CHECK(width(ModbusType::boolean) == 1);
    CHECK(width(ModbusType::int16) == 1);
    CHECK(width(ModbusType::uint16) == 1);
    CHECK(width(ModbusType::int32) == 2);
    CHECK(width(ModbusType::uint32) == 2);
    CHECK(width(ModbusType::float32) == 2);
}

TEST_CASE("plan_scan_groups of no tags is empty", "[modbus]") {
    CHECK(plan_scan_groups({}).empty());
}

TEST_CASE("plan_scan_groups reads a single tag in one block", "[modbus]") {
    const std::vector tags{holding(10, ModbusType::float32)};

    const auto groups = plan_scan_groups(tags);

    REQUIRE(groups.size() == 1);
    CHECK(groups[0].period == 1000ms);
    REQUIRE(groups[0].blocks.size() == 1);
    check_block(groups[0].blocks[0], ModbusArea::holding, 10, 2, {0});
}

TEST_CASE("plan_scan_groups merges contiguous tags", "[modbus]") {
    const std::vector tags{holding(0, ModbusType::uint32), holding(2), holding(3)};

    const auto groups = plan_scan_groups(tags);

    REQUIRE(groups.size() == 1);
    REQUIRE(groups[0].blocks.size() == 1);
    check_block(groups[0].blocks[0], ModbusArea::holding, 0, 4, {0, 1, 2});
}

TEST_CASE("plan_scan_groups merges overlapping tags", "[modbus]") {
    SECTION("uint16 inside a uint32") {
        const std::vector tags{holding(0, ModbusType::uint32), holding(1)};

        const auto groups = plan_scan_groups(tags);

        REQUIRE(groups.size() == 1);
        REQUIRE(groups[0].blocks.size() == 1);
        check_block(groups[0].blocks[0], ModbusArea::holding, 0, 2, {0, 1});
    }
    SECTION("uint32 starting inside another uint32") {
        const std::vector tags{holding(0, ModbusType::uint32), holding(1, ModbusType::int32)};

        const auto groups = plan_scan_groups(tags);

        REQUIRE(groups.size() == 1);
        REQUIRE(groups[0].blocks.size() == 1);
        check_block(groups[0].blocks[0], ModbusArea::holding, 0, 3, {0, 1});
    }
    SECTION("two tags at the same address") {
        const std::vector tags{holding(5), holding(5, ModbusType::int16)};

        const auto groups = plan_scan_groups(tags);

        REQUIRE(groups.size() == 1);
        REQUIRE(groups[0].blocks.size() == 1);
        check_block(groups[0].blocks[0], ModbusArea::holding, 5, 1, {0, 1});
    }
}

TEST_CASE("plan_scan_groups splits at a gap", "[modbus]") {
    const std::vector tags{holding(0), holding(2)};

    const auto groups = plan_scan_groups(tags);

    REQUIRE(groups.size() == 1);
    REQUIRE(groups[0].blocks.size() == 2);
    check_block(groups[0].blocks[0], ModbusArea::holding, 0, 1, {0});
    check_block(groups[0].blocks[1], ModbusArea::holding, 2, 1, {1});
}

TEST_CASE("plan_scan_groups splits blocks at the per-request maximum", "[modbus]") {
    SECTION("registers") {
        std::vector<TagConfig> tags;
        for (std::uint16_t a = 0; a < max_registers_per_read + 1; ++a) {
            tags.push_back(holding(a));
        }

        const auto groups = plan_scan_groups(tags);

        REQUIRE(groups.size() == 1);
        REQUIRE(groups[0].blocks.size() == 2);
        CHECK(groups[0].blocks[0].start == 0);
        CHECK(groups[0].blocks[0].count == max_registers_per_read);
        CHECK(groups[0].blocks[0].tags.size() == max_registers_per_read);
        check_block(groups[0].blocks[1], ModbusArea::holding, max_registers_per_read, 1,
                    {max_registers_per_read});
    }
    SECTION("a 32-bit tag that would cross the maximum starts a new block") {
        std::vector<TagConfig> tags;
        for (std::uint16_t a = 0; a < max_registers_per_read - 1; ++a) {
            tags.push_back(holding(a));
        }
        tags.push_back(holding(max_registers_per_read - 1, ModbusType::uint32));

        const auto groups = plan_scan_groups(tags);

        REQUIRE(groups.size() == 1);
        REQUIRE(groups[0].blocks.size() == 2);
        CHECK(groups[0].blocks[0].count == max_registers_per_read - 1);
        check_block(groups[0].blocks[1], ModbusArea::holding, max_registers_per_read - 1, 2,
                    {max_registers_per_read - 1});
    }
    SECTION("bits") {
        std::vector<TagConfig> tags;
        for (std::uint16_t a = 0; a < max_bits_per_read + 1; ++a) {
            tags.push_back(coil(a));
        }

        const auto groups = plan_scan_groups(tags);

        REQUIRE(groups.size() == 1);
        REQUIRE(groups[0].blocks.size() == 2);
        CHECK(groups[0].blocks[0].start == 0);
        CHECK(groups[0].blocks[0].count == max_bits_per_read);
        check_block(groups[0].blocks[1], ModbusArea::coil, max_bits_per_read, 1,
                    {max_bits_per_read});
    }
}

TEST_CASE("plan_scan_groups never merges different areas", "[modbus]") {
    const std::vector tags{tag(ModbusArea::input, 0, ModbusType::uint16), holding(0),
                           tag(ModbusArea::discrete_input, 0, ModbusType::boolean), coil(0)};

    const auto groups = plan_scan_groups(tags);

    REQUIRE(groups.size() == 1);
    REQUIRE(groups[0].blocks.size() == 4);
    // Ordered by (area, start), whatever the order in the configuration.
    check_block(groups[0].blocks[0], ModbusArea::coil, 0, 1, {3});
    check_block(groups[0].blocks[1], ModbusArea::discrete_input, 0, 1, {2});
    check_block(groups[0].blocks[2], ModbusArea::holding, 0, 1, {1});
    check_block(groups[0].blocks[3], ModbusArea::input, 0, 1, {0});
}

TEST_CASE("plan_scan_groups puts different periods in different groups", "[modbus]") {
    const std::vector tags{holding(0, ModbusType::uint16, 5000ms), holding(1),
                           holding(2, ModbusType::uint16, 100ms)};

    const auto groups = plan_scan_groups(tags);

    // Ascending period; adjacent addresses are not merged across groups.
    REQUIRE(groups.size() == 3);
    CHECK(groups[0].period == 100ms);
    CHECK(groups[1].period == 1000ms);
    CHECK(groups[2].period == 5000ms);
    REQUIRE(groups[0].blocks.size() == 1);
    REQUIRE(groups[1].blocks.size() == 1);
    REQUIRE(groups[2].blocks.size() == 1);
    check_block(groups[0].blocks[0], ModbusArea::holding, 2, 1, {2});
    check_block(groups[1].blocks[0], ModbusArea::holding, 1, 1, {1});
    check_block(groups[2].blocks[0], ModbusArea::holding, 0, 1, {0});
}

TEST_CASE("plan_scan_groups orders blocks by address, not configuration order", "[modbus]") {
    const std::vector tags{holding(20), holding(3), holding(10), holding(2)};

    const auto groups = plan_scan_groups(tags);

    REQUIRE(groups.size() == 1);
    REQUIRE(groups[0].blocks.size() == 3);
    check_block(groups[0].blocks[0], ModbusArea::holding, 2, 2, {3, 1});
    check_block(groups[0].blocks[1], ModbusArea::holding, 10, 1, {2});
    check_block(groups[0].blocks[2], ModbusArea::holding, 20, 1, {0});
    CHECK(plan_scan_groups(tags) == groups); // same input, same plan
}

TEST_CASE("plan_scan_groups reads the example plant in three requests", "[modbus]") {
    const auto loaded = f2c::config::load_plant_config(F2C_PLANT1_YAML);
    REQUIRE(loaded);
    REQUIRE(loaded->plant.devices.size() == 1);
    const auto& tags = loaded->plant.devices[0].tags;

    const auto groups = plan_scan_groups(tags);

    REQUIRE(groups.size() == 1);
    CHECK(groups[0].period == 1000ms);
    REQUIRE(groups[0].blocks.size() == 3);
    // counter, speed, speed_setpoint, state | in_fault | temperature
    check_block(groups[0].blocks[0], ModbusArea::coil, 0, 1, {5});
    check_block(groups[0].blocks[1], ModbusArea::holding, 0, 5, {0, 1, 2, 3});
    check_block(groups[0].blocks[2], ModbusArea::input, 0, 1, {4});
}
