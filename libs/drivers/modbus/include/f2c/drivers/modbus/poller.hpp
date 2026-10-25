#pragma once

#include "f2c/config/plant_config.hpp"
#include "f2c/core/backoff.hpp"
#include "f2c/core/clock.hpp"
#include "f2c/core/tag_sample.hpp"
#include "f2c/drivers/modbus/modbus_client.hpp"
#include "f2c/drivers/modbus/read_plan.hpp"

#include <chrono>
#include <cstddef>
#include <vector>

namespace f2c::drivers::modbus {

// Functional core of the Modbus driver: decides what to read and when.
// Owns no thread and no timer; the clock and the client are injected, so every
// scanning and reconnection rule is testable without a network or real time.
class Poller {
public:
    Poller(config::DeviceConfig device, ModbusClient& client, const core::Clock& clock);

    struct Result {
        std::vector<core::TagSample> samples;
        std::chrono::steady_clock::time_point next_wakeup;
    };

    // One step:
    //  - disconnected and backoff not elapsed → no I/O, next_wakeup = retry time;
    //  - disconnected → connect(); on failure emit bad samples once per outage, schedule retry;
    //    on success reset backoff and make every group due now;
    //  - connected → read every due block; good samples on success; bad samples for the block's
    //    tags on protocol_error; on io_error/timeout close, emit bad for all tags, schedule retry.
    // Group schedule is fixed-rate (due += period); if a group fell behind, it is rescheduled
    // to now + period (missed scans are skipped, never burst).
    Result poll();

    [[nodiscard]] bool connected() const noexcept;

private:
    using TimePoint = std::chrono::steady_clock::time_point;

    // Appends the block's samples. False when the connection was lost (nothing appended).
    bool read_block(const ReadBlock& block, std::vector<core::TagSample>& samples);
    // Closes the connection, reports every tag bad once per outage, schedules the retry.
    Result fail_connection();
    [[nodiscard]] core::TagSample sample(std::size_t tag, core::Value value, core::Quality quality,
                                         core::Timestamp time) const;

    config::DeviceConfig device_;
    ModbusClient* client_;     // non-owning, never null
    const core::Clock* clock_; // never null
    std::vector<ScanGroup> groups_;
    std::vector<TimePoint> due_;           // per group
    std::vector<core::Value> last_values_; // per tag; monostate until first read
    core::Backoff backoff_;
    TimePoint retry_at_;
    bool outage_reported_{false};
};

} // namespace f2c::drivers::modbus
