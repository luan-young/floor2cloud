#pragma once

#include "f2c/core/tag_path.hpp"
#include "f2c/core/tag_sample.hpp"

#include <chrono>
#include <optional>
#include <unordered_map>
#include <vector>

namespace f2c::messaging {

// Publish-on-change with a per-tag heartbeat (spec § 6.3). Pure, single-threaded.
class ChangeFilter {
public:
    using TimePoint = std::chrono::steady_clock::time_point;

    explicit ChangeFilter(std::chrono::seconds heartbeat); // 0 disables heartbeats

    // Records the sample as the tag's latest. Returns true (and marks it published at `now`)
    // if it is the tag's first sample or its value or quality differs from the last published one.
    // Values compare exactly (no deadband in M1).
    bool offer(const core::TagSample& sample, TimePoint now);

    // Latest sample of every tag whose last publish is at least `heartbeat` old, ordered by path;
    // marks them published.
    std::vector<core::TagSample> take_heartbeats(TimePoint now);

    // When the earliest heartbeat is due; empty if heartbeats are disabled or no tag was offered.
    [[nodiscard]] std::optional<TimePoint> next_heartbeat() const;

private:
    struct TagState {
        core::TagSample latest;
        core::TagSample published;
        TimePoint published_at;
    };

    std::chrono::seconds heartbeat_;
    std::unordered_map<core::TagPath, TagState> tags_;
};

} // namespace f2c::messaging
