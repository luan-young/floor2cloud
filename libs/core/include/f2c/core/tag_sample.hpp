#pragma once

#include "f2c/core/error.hpp"
#include "f2c/core/tag_path.hpp"
#include "f2c/core/timestamp.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <variant>

namespace f2c::core {

// std::monostate = "no value": a bad-quality sample of a tag that has never been read.
using Value = std::variant<std::monostate, bool, std::int64_t, double, std::string>;

enum class Quality : std::uint8_t { good, uncertain, bad }; // M1 drivers emit only good and bad

[[nodiscard]] std::string_view to_string(Quality q) noexcept; // "good" | "uncertain" | "bad"
[[nodiscard]] Expected<Quality> parse_quality(std::string_view text);

struct TagSample {
    TagPath path;
    Value value;
    Quality quality;
    Timestamp source_time;  // when the source produced it; the read-completion time if it has none
    Timestamp gateway_time; // when the gateway processed it (set by SamplePublisher; drivers copy
                            // source_time)

    friend bool operator==(const TagSample&, const TagSample&) = default;
};

} // namespace f2c::core
