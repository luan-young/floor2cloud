#include "f2c/core/tag_sample.hpp"

#include <string>

namespace f2c::core {

std::string_view to_string(Quality q) noexcept {
    switch (q) {
    case Quality::good:
        return "good";
    case Quality::uncertain:
        return "uncertain";
    case Quality::bad:
        return "bad";
    }
    return "unknown";
}

Expected<Quality> parse_quality(std::string_view text) {
    for (const auto q : {Quality::good, Quality::uncertain, Quality::bad}) {
        if (text == to_string(q)) {
            return q;
        }
    }
    std::string message = "unknown quality \"";
    message += text;
    message += "\" (expected good, uncertain or bad)";
    return make_error(Errc::invalid_argument, std::move(message));
}

} // namespace f2c::core
