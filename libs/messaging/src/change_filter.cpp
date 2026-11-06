#include "f2c/messaging/change_filter.hpp"

#include <algorithm>

namespace f2c::messaging {

ChangeFilter::ChangeFilter(std::chrono::seconds heartbeat) : heartbeat_{heartbeat} {}

bool ChangeFilter::offer(const core::TagSample& sample, TimePoint now) {
    const auto [it, inserted] = tags_.try_emplace(sample.path, TagState{sample, sample, now});
    if (inserted) {
        return true;
    }
    auto& state = it->second;
    state.latest = sample;
    if (sample.value == state.published.value && sample.quality == state.published.quality) {
        return false;
    }
    state.published = sample;
    state.published_at = now;
    return true;
}

std::vector<core::TagSample> ChangeFilter::take_heartbeats(TimePoint now) {
    std::vector<core::TagSample> due;
    if (heartbeat_ == std::chrono::seconds::zero()) {
        return due;
    }
    for (auto& [path, state] : tags_) {
        if (now - state.published_at >= heartbeat_) {
            state.published = state.latest;
            state.published_at = now;
            due.push_back(state.latest);
        }
    }
    std::ranges::sort(due, {}, &core::TagSample::path);
    return due;
}

std::optional<ChangeFilter::TimePoint> ChangeFilter::next_heartbeat() const {
    if (heartbeat_ == std::chrono::seconds::zero() || tags_.empty()) {
        return std::nullopt;
    }
    const auto earliest = std::ranges::min_element(
        tags_, {}, [](const auto& entry) { return entry.second.published_at; });
    return earliest->second.published_at + heartbeat_;
}

} // namespace f2c::messaging
