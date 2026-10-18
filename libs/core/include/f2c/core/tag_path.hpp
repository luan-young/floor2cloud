#pragma once

#include "f2c/core/error.hpp"

#include <compare>
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace f2c::core {

inline constexpr std::size_t max_tag_path_depth = 16;
inline constexpr std::size_t max_segment_length = 64;

// ISA-95 address: site/area/line/cell/equipment/tag, slash-separated.
// Invariants (checked on construction):
//   - 1 to 16 segments;
//   - each segment matches [a-z0-9][a-z0-9_-]{0,63}.
// Lowercase only, so "Filler" and "filler" can never be two topics or two series.
// Segments starting with '_' are reserved for system topics (e.g. "_gateway") and rejected.
// The ISA-95 depth is a convention of the configuration, not enforced here.
class TagPath {
public:
    static Expected<TagPath> parse(std::string_view text);

    // Fails if the result would break an invariant.
    [[nodiscard]] Expected<TagPath> append(std::string_view segment) const;
    [[nodiscard]] Expected<TagPath> join(const TagPath& suffix) const;

    [[nodiscard]] const std::string& str() const noexcept { return text_; }
    [[nodiscard]] std::vector<std::string_view> segments() const;
    [[nodiscard]] std::size_t depth() const noexcept;

    friend bool operator==(const TagPath&, const TagPath&) = default;
    friend auto operator<=>(const TagPath&, const TagPath&) = default;

private:
    explicit TagPath(std::string text);
    std::string text_;
};

[[nodiscard]] bool is_valid_segment(std::string_view segment) noexcept;

} // namespace f2c::core

template <>
struct std::hash<f2c::core::TagPath> {
    std::size_t operator()(const f2c::core::TagPath& p) const noexcept;
};
