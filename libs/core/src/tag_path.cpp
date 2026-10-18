#include "f2c/core/tag_path.hpp"

#include <algorithm>
#include <string>
#include <utility>

namespace f2c::core {

namespace {

bool is_lower_alnum(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
}

tl::unexpected<Error> invalid(std::string_view text, std::string_view reason) {
    std::string message = "invalid tag path \"";
    message += text;
    message += "\": ";
    message += reason;
    return make_error(Errc::invalid_argument, std::move(message));
}

std::string segment_reason(std::size_t index, std::string_view segment) {
    std::string reason = "segment " + std::to_string(index + 1) + " \"";
    reason += segment;
    reason += "\" must match [a-z0-9][a-z0-9_-]{0,63}";
    return reason;
}

} // namespace

bool is_valid_segment(std::string_view segment) noexcept {
    if (segment.empty() || segment.size() > max_segment_length) {
        return false;
    }
    if (!is_lower_alnum(segment.front())) {
        return false;
    }
    return std::ranges::all_of(segment.substr(1),
                               [](char c) { return is_lower_alnum(c) || c == '_' || c == '-'; });
}

Expected<TagPath> TagPath::parse(std::string_view text) {
    std::size_t index = 0;
    std::size_t begin = 0;
    while (true) {
        if (index == max_tag_path_depth) {
            return invalid(text, "more than " + std::to_string(max_tag_path_depth) + " segments");
        }
        const auto end = std::min(text.find('/', begin), text.size());
        const auto segment = text.substr(begin, end - begin);
        if (!is_valid_segment(segment)) {
            return invalid(text, segment_reason(index, segment));
        }
        ++index;
        if (end == text.size()) {
            break;
        }
        begin = end + 1;
    }
    return TagPath{std::string{text}};
}

Expected<TagPath> TagPath::append(std::string_view segment) const {
    if (!is_valid_segment(segment)) {
        return invalid(text_, "cannot append: " + segment_reason(depth(), segment));
    }
    if (depth() == max_tag_path_depth) {
        return invalid(text_, "cannot append: already " + std::to_string(max_tag_path_depth) +
                                  " segments");
    }
    std::string text = text_;
    text += '/';
    text += segment;
    return TagPath{std::move(text)};
}

Expected<TagPath> TagPath::join(const TagPath& suffix) const {
    if (depth() + suffix.depth() > max_tag_path_depth) {
        return invalid(text_ + '/' + suffix.text_,
                       "more than " + std::to_string(max_tag_path_depth) + " segments");
    }
    return TagPath{text_ + '/' + suffix.text_};
}

std::vector<std::string_view> TagPath::segments() const {
    std::vector<std::string_view> result;
    result.reserve(depth());
    const std::string_view text = text_;
    std::size_t begin = 0;
    while (true) {
        const auto end = std::min(text.find('/', begin), text.size());
        result.push_back(text.substr(begin, end - begin));
        if (end == text.size()) {
            return result;
        }
        begin = end + 1;
    }
}

std::size_t TagPath::depth() const noexcept {
    return static_cast<std::size_t>(std::ranges::count(text_, '/')) + 1;
}

TagPath::TagPath(std::string text) : text_(std::move(text)) {}

} // namespace f2c::core

std::size_t std::hash<f2c::core::TagPath>::operator()(const f2c::core::TagPath& p) const noexcept {
    return std::hash<std::string>{}(p.str());
}
