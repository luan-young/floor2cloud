#include "f2c/core/testing/manual_clock.hpp"
#include "f2c/drivers/modbus/poller.hpp"
#include "f2c/drivers/modbus/testing/fake_modbus_client.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

using namespace std::chrono_literals;
using f2c::config::ByteOrder;
using f2c::config::DeviceConfig;
using f2c::config::LinearScaling;
using f2c::config::ModbusAddress;
using f2c::config::ModbusArea;
using f2c::config::ModbusTcpConfig;
using f2c::config::ModbusType;
using f2c::config::TagConfig;
using f2c::config::WordOrder;
using f2c::core::Errc;
using f2c::core::Quality;
using f2c::core::TagSample;
using f2c::core::Value;
using f2c::core::testing::ManualClock;
using f2c::drivers::modbus::Poller;
using f2c::drivers::modbus::testing::FakeModbusClient;
using Read = FakeModbusClient::Read;

namespace {

constexpr std::string_view device_path = "plant1/production/line1/cell-c/filler";

TagConfig tag(std::string name, ModbusArea area, std::uint16_t address, ModbusType type,
              std::chrono::milliseconds period = 1000ms,
              std::optional<LinearScaling> scaling = std::nullopt) {
    auto path = f2c::core::TagPath::parse(std::string{device_path} + "/" + name);
    REQUIRE(path);
    return TagConfig{.name = std::move(name),
                     .path = *std::move(path),
                     .scan_period = period,
                     .scaling = scaling,
                     .address = ModbusAddress{.area = area,
                                              .address = address,
                                              .type = type,
                                              .byte_order = ByteOrder::big,
                                              .word_order = WordOrder::big}};
}

DeviceConfig device(std::vector<TagConfig> tags) {
    auto path = f2c::core::TagPath::parse(device_path);
    REQUIRE(path);
    return DeviceConfig{.id = "filler",
                        .path = *std::move(path),
                        .driver = ModbusTcpConfig{.host = "localhost",
                                                  .timeout = 500ms,
                                                  .reconnect = {.initial = 500ms, .max = 10'000ms}},
                        .tags = std::move(tags)};
}

// The § 5.1 filler, minus the tags that add nothing here: three blocks per scan.
DeviceConfig filler() {
    return device({tag("counter", ModbusArea::holding, 0, ModbusType::uint32),
                   tag("speed", ModbusArea::holding, 2, ModbusType::uint16),
                   tag("temperature", ModbusArea::input, 0, ModbusType::int16, 1000ms,
                       LinearScaling{.scale = 0.1, .offset = 0.0}),
                   tag("in_fault", ModbusArea::coil, 0, ModbusType::boolean)});
}

void load_filler_values(FakeModbusClient& client) {
    client.set_register(ModbusArea::holding, 0, 0x0001); // counter = 65538
    client.set_register(ModbusArea::holding, 1, 0x0002);
    client.set_register(ModbusArea::holding, 2, 120); // speed
    client.set_register(ModbusArea::input, 0, 250);   // temperature = 25.0
    client.set_bit(ModbusArea::coil, 0, true);        // in_fault
}

const TagSample& sample_of(const std::vector<TagSample>& samples, std::string_view name) {
    const auto it = std::ranges::find_if(samples, [name](const TagSample& s) {
        return s.path.str() == std::string{device_path} + "/" + std::string{name};
    });
    REQUIRE(it != samples.end());
    return *it;
}

bool all_bad(const std::vector<TagSample>& samples) {
    return std::ranges::all_of(samples,
                               [](const TagSample& s) { return s.quality == Quality::bad; });
}

bool all_good(const std::vector<TagSample>& samples) {
    return std::ranges::all_of(samples,
                               [](const TagSample& s) { return s.quality == Quality::good; });
}

// Advances the clock to `when` (never backwards) and polls.
Poller::Result poll_at(Poller& poller, ManualClock& clock,
                       std::chrono::steady_clock::time_point when) {
    if (when > clock.steady_now()) {
        clock.advance(when - clock.steady_now());
    }
    return poller.poll();
}

} // namespace

TEST_CASE("Poller connects and reads every group on the first poll", "[modbus]") {
    ManualClock clock;
    FakeModbusClient client{&clock};
    load_filler_values(client);
    Poller poller{filler(), client, clock};
    const auto t0 = clock.steady_now();

    const auto result = poller.poll();

    CHECK(client.connect_calls() == 1);
    CHECK(poller.connected());
    CHECK(client.reads() == std::vector<Read>{{ModbusArea::coil, 0, 1},
                                              {ModbusArea::holding, 0, 3},
                                              {ModbusArea::input, 0, 1}});
    REQUIRE(result.samples.size() == 4);
    CHECK(all_good(result.samples));
    CHECK(sample_of(result.samples, "counter").value == Value{std::int64_t{65538}});
    CHECK(sample_of(result.samples, "speed").value == Value{std::int64_t{120}});
    CHECK(sample_of(result.samples, "temperature").value == Value{25.0});
    CHECK(sample_of(result.samples, "in_fault").value == Value{true});
    for (const auto& sample : result.samples) {
        CHECK(sample.source_time == clock.now());
        CHECK(sample.gateway_time == sample.source_time);
    }
    CHECK(result.next_wakeup == t0 + 1s);
}

TEST_CASE("Poller reads groups with different periods at their own rates", "[modbus]") {
    ManualClock clock;
    FakeModbusClient client{&clock};
    Poller poller{device({tag("fast", ModbusArea::holding, 0, ModbusType::uint16, 250ms),
                          tag("slow", ModbusArea::holding, 1, ModbusType::uint16, 1000ms)}),
                  client, clock};
    const auto t0 = clock.steady_now();

    auto result = poller.poll();
    CHECK(result.samples.size() == 2);
    CHECK(result.next_wakeup == t0 + 250ms); // the earliest group deadline

    int fast = 0;
    int slow = 0;
    for (int step = 0; step < 100 && result.next_wakeup <= t0 + 2s; ++step) {
        REQUIRE(result.next_wakeup > clock.steady_now());
        result = poll_at(poller, clock, result.next_wakeup);
        for (const auto& sample : result.samples) {
            const bool is_fast = sample.path.str().ends_with("/fast");
            (is_fast ? fast : slow) += 1;
        }
    }

    // 250, 500, …, 2000 ms and 1000, 2000 ms.
    CHECK(fast == 8);
    CHECK(slow == 2);
}

TEST_CASE("Poller does no I/O before a group is due", "[modbus]") {
    ManualClock clock;
    FakeModbusClient client{&clock};
    Poller poller{filler(), client, clock};
    const auto t0 = clock.steady_now();
    (void)poller.poll();
    client.clear_reads();

    const auto result = poll_at(poller, clock, t0 + 400ms);

    CHECK(result.samples.empty());
    CHECK(client.reads().empty());
    CHECK(result.next_wakeup == t0 + 1s);
}

TEST_CASE("Poller schedules at a fixed rate and skips missed scans", "[modbus]") {
    ManualClock clock;
    FakeModbusClient client{&clock};
    Poller poller{filler(), client, clock};
    const auto t0 = clock.steady_now();
    (void)poller.poll();

    SECTION("a late wakeup does not shift the schedule") {
        const auto result = poll_at(poller, clock, t0 + 1030ms);

        CHECK(result.samples.size() == 4);
        CHECK(result.next_wakeup == t0 + 2s);
    }
    SECTION("a group that fell behind reads once and resumes one period later") {
        client.clear_reads();

        const auto late = poll_at(poller, clock, t0 + 3500ms);
        const auto again = poller.poll();

        CHECK(late.samples.size() == 4);
        CHECK(client.reads().size() == 3); // one scan, not a burst of three
        CHECK(late.next_wakeup == t0 + 4500ms);
        CHECK(again.samples.empty());
    }
}

TEST_CASE("Poller reports a failed initial connection once and retries with backoff", "[modbus]") {
    ManualClock clock;
    FakeModbusClient client{&clock};
    client.set_responding(false);
    Poller poller{filler(), client, clock};
    const auto t0 = clock.steady_now();

    const auto first = poller.poll();

    CHECK_FALSE(poller.connected());
    REQUIRE(first.samples.size() == 4);
    CHECK(all_bad(first.samples));
    for (const auto& sample : first.samples) {
        CHECK(std::holds_alternative<std::monostate>(sample.value));
        CHECK(sample.source_time == clock.now());
    }
    CHECK(first.next_wakeup == t0 + 500ms);

    SECTION("no I/O before the retry time") {
        const auto early = poll_at(poller, clock, t0 + 200ms);

        CHECK(early.samples.empty());
        CHECK(client.connect_calls() == 1);
        CHECK(client.reads().empty());
        CHECK(early.next_wakeup == t0 + 500ms);
    }
    SECTION("further failures emit nothing and back off") {
        const auto second = poll_at(poller, clock, first.next_wakeup);
        const auto third = poll_at(poller, clock, second.next_wakeup);

        CHECK(client.connect_calls() == 3);
        CHECK(second.samples.empty());
        CHECK(third.samples.empty());
        CHECK(second.next_wakeup == t0 + 500ms + 1000ms);
        CHECK(third.next_wakeup == t0 + 500ms + 1000ms + 2000ms);
    }
}

TEST_CASE("Poller handles a device that stops responding while connected", "[modbus]") {
    ManualClock clock;
    FakeModbusClient client{&clock, 500ms};
    load_filler_values(client);
    Poller poller{filler(), client, clock};
    const auto t0 = clock.steady_now();
    (void)poller.poll();

    client.set_responding(false);
    const auto lost = poll_at(poller, clock, t0 + 1s);

    // The first read times out after 500 ms; no further read is attempted.
    CHECK_FALSE(poller.connected());
    CHECK(client.close_calls() == 1);
    REQUIRE(lost.samples.size() == 4);
    CHECK(all_bad(lost.samples));
    CHECK(sample_of(lost.samples, "counter").value == Value{std::int64_t{65538}});
    CHECK(sample_of(lost.samples, "temperature").value == Value{25.0});
    CHECK(sample_of(lost.samples, "in_fault").value == Value{true});
    for (const auto& sample : lost.samples) {
        CHECK(sample.source_time == clock.now());
    }
    CHECK(clock.steady_now() == t0 + 1500ms);
    CHECK(lost.next_wakeup == t0 + 1500ms + 500ms);

    SECTION("repeated failures emit nothing and back off 500 → 1000 → 2000") {
        const auto second = poll_at(poller, clock, lost.next_wakeup);
        const auto third = poll_at(poller, clock, second.next_wakeup);

        CHECK(second.samples.empty());
        CHECK(third.samples.empty());
        CHECK(second.next_wakeup - lost.next_wakeup == 1000ms);
        CHECK(third.next_wakeup - second.next_wakeup == 2000ms);
    }
    SECTION("recovery reads good samples on the first poll after reconnecting") {
        (void)poll_at(poller, clock, lost.next_wakeup); // still down: backoff now at 1000 ms
        client.set_responding(true);
        client.set_register(ModbusArea::holding, 2, 90);
        const auto reconnect_at = clock.steady_now() + 1000ms;

        const auto recovered = poll_at(poller, clock, reconnect_at);

        CHECK(poller.connected());
        REQUIRE(recovered.samples.size() == 4);
        CHECK(all_good(recovered.samples));
        CHECK(sample_of(recovered.samples, "speed").value == Value{std::int64_t{90}});
        CHECK(recovered.next_wakeup == reconnect_at + 1s);

        SECTION("and the backoff starts over at the next outage") {
            client.set_responding(false);
            const auto lost_again = poll_at(poller, clock, recovered.next_wakeup);

            REQUIRE(lost_again.samples.size() == 4);
            CHECK(all_bad(lost_again.samples));
            CHECK(lost_again.next_wakeup == clock.steady_now() + 500ms);
        }
    }
}

TEST_CASE("Poller marks only the tags of a block that got a Modbus exception", "[modbus]") {
    ManualClock clock;
    FakeModbusClient client{&clock};
    load_filler_values(client);
    Poller poller{filler(), client, clock};
    const auto t0 = clock.steady_now();
    (void)poller.poll();

    client.fail_reads_at(ModbusArea::holding, 0, Errc::protocol_error);
    const auto result = poll_at(poller, clock, t0 + 1s);

    CHECK(poller.connected());
    CHECK(client.close_calls() == 0);
    REQUIRE(result.samples.size() == 4);
    CHECK(sample_of(result.samples, "counter").quality == Quality::bad);
    CHECK(sample_of(result.samples, "counter").value == Value{std::int64_t{65538}});
    CHECK(sample_of(result.samples, "speed").quality == Quality::bad);
    CHECK(sample_of(result.samples, "temperature").quality == Quality::good);
    CHECK(sample_of(result.samples, "in_fault").quality == Quality::good);
    CHECK(result.next_wakeup == t0 + 2s);
}

TEST_CASE("Poller marks only the tag that fails to decode", "[modbus]") {
    ManualClock clock;
    FakeModbusClient client{&clock};
    client.set_register(ModbusArea::holding, 0, 0x7FC0); // float32 quiet NaN
    client.set_register(ModbusArea::holding, 1, 0x0000);
    client.set_register(ModbusArea::holding, 2, 7);
    Poller poller{device({tag("flow", ModbusArea::holding, 0, ModbusType::float32),
                          tag("level", ModbusArea::holding, 2, ModbusType::uint16)}),
                  client, clock};

    const auto result = poller.poll();

    REQUIRE(result.samples.size() == 2);
    CHECK(sample_of(result.samples, "flow").quality == Quality::bad);
    CHECK(std::holds_alternative<std::monostate>(sample_of(result.samples, "flow").value));
    CHECK(sample_of(result.samples, "level").quality == Quality::good);
    CHECK(sample_of(result.samples, "level").value == Value{std::int64_t{7}});
}

TEST_CASE("Poller turns tags bad within two scans of the device stopping (criterion 3)",
          "[modbus]") {
    // 1 s scan, 500 ms timeout. The worst case is the device stopping just after a good read.
    ManualClock clock;
    FakeModbusClient client{&clock, 500ms};
    Poller poller{filler(), client, clock};
    auto result = poller.poll();
    REQUIRE(all_good(result.samples));

    clock.advance(1ms);
    client.set_responding(false);
    const auto stopped_at = clock.now();

    bool went_bad = false;
    f2c::core::Timestamp bad_at;
    for (int step = 0; step < 100 && !went_bad; ++step) {
        REQUIRE(result.next_wakeup > clock.steady_now());
        result = poll_at(poller, clock, result.next_wakeup);
        if (!result.samples.empty() && all_bad(result.samples)) {
            went_bad = true;
            bad_at = result.samples.front().source_time;
        }
    }

    REQUIRE(went_bad);
    CHECK(bad_at - stopped_at <= 2s);
}
