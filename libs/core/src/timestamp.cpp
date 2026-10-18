#include "f2c/core/timestamp.hpp"

#include <cstdint>
#include <format>
#include <optional>
#include <string>

namespace f2c::core {

namespace {

using namespace std::chrono;

// Reads exactly `count` ASCII digits at `pos` and advances past them.
std::optional<int> read_digits(std::string_view text, std::size_t& pos, std::size_t count) {
    if (text.size() - pos < count) {
        return std::nullopt;
    }
    int value = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const char c = text[pos + i];
        if (c < '0' || c > '9') {
            return std::nullopt;
        }
        value = value * 10 + (c - '0');
    }
    pos += count;
    return value;
}

// Consumes `expected` (or its lowercase form, if `lowercase` is not '\0') at `pos`.
bool read_char(std::string_view text, std::size_t& pos, char expected, char lowercase = '\0') {
    if (pos >= text.size() ||
        (text[pos] != expected && (lowercase == '\0' || text[pos] != lowercase))) {
        return false;
    }
    ++pos;
    return true;
}

tl::unexpected<Error> invalid(std::string_view text, std::string_view reason) {
    std::string message = "invalid RFC 3339 timestamp \"";
    message += text;
    message += "\": ";
    message += reason;
    return make_error(Errc::parse_error, std::move(message));
}

} // namespace

std::string format_rfc3339_ms(Timestamp t) {
    const auto ms = floor<milliseconds>(t);
    const auto day = floor<days>(ms);
    const year_month_day ymd{day};
    const hh_mm_ss time{ms - day};
    return std::format("{:04}-{:02}-{:02}T{:02}:{:02}:{:02}.{:03}Z", static_cast<int>(ymd.year()),
                       static_cast<unsigned>(ymd.month()), static_cast<unsigned>(ymd.day()),
                       time.hours().count(), time.minutes().count(), time.seconds().count(),
                       time.subseconds().count());
}

Expected<Timestamp> parse_rfc3339(std::string_view text) {
    std::size_t pos = 0;

    // date-fullyear "-" date-month "-" date-mday
    constexpr std::string_view bad_date = "expected a YYYY-MM-DD date";
    const auto y = read_digits(text, pos, 4);
    if (!y || !read_char(text, pos, '-')) {
        return invalid(text, bad_date);
    }
    const auto mo = read_digits(text, pos, 2);
    if (!mo || !read_char(text, pos, '-')) {
        return invalid(text, bad_date);
    }
    const auto d = read_digits(text, pos, 2);
    if (!d) {
        return invalid(text, bad_date);
    }
    const year_month_day ymd{year{*y}, month{static_cast<unsigned>(*mo)},
                             day{static_cast<unsigned>(*d)}};
    if (!ymd.ok()) {
        return invalid(text, "no such date");
    }

    if (!read_char(text, pos, 'T', 't')) {
        return invalid(text, "expected 'T' between date and time");
    }

    // time-hour ":" time-minute ":" time-second
    constexpr std::string_view bad_time = "expected an hh:mm:ss time";
    const auto h = read_digits(text, pos, 2);
    if (!h || !read_char(text, pos, ':')) {
        return invalid(text, bad_time);
    }
    const auto mi = read_digits(text, pos, 2);
    if (!mi || !read_char(text, pos, ':')) {
        return invalid(text, bad_time);
    }
    const auto s = read_digits(text, pos, 2);
    if (!s) {
        return invalid(text, bad_time);
    }
    if (*h > 23 || *mi > 59 || *s > 59) {
        return invalid(text, "time out of range");
    }

    // [time-secfrac]: 1 to 9 digits, kept to the nanosecond
    nanoseconds fraction{0};
    if (read_char(text, pos, '.')) {
        std::int64_t scale = 100'000'000;
        std::size_t digits = 0;
        while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') {
            if (++digits > 9) {
                return invalid(text, "more than 9 fractional digits");
            }
            fraction += nanoseconds{(text[pos] - '0') * scale};
            scale /= 10;
            ++pos;
        }
        if (digits == 0) {
            return invalid(text, "expected digits after '.'");
        }
    }

    // time-offset: "Z" / ("+" / "-") time-hour ":" time-minute
    minutes offset{0};
    if (pos < text.size() && (text[pos] == '+' || text[pos] == '-')) {
        const bool negative = text[pos] == '-';
        ++pos;
        constexpr std::string_view bad_offset = "expected a ±hh:mm offset";
        const auto oh = read_digits(text, pos, 2);
        if (!oh || !read_char(text, pos, ':')) {
            return invalid(text, bad_offset);
        }
        const auto om = read_digits(text, pos, 2);
        if (!om) {
            return invalid(text, bad_offset);
        }
        if (*oh > 23 || *om > 59) {
            return invalid(text, "offset out of range");
        }
        offset = hours{*oh} + minutes{*om};
        if (negative) {
            offset = -offset;
        }
    } else if (!read_char(text, pos, 'Z', 'z')) {
        return invalid(text, "expected 'Z' or a ±hh:mm offset");
    }

    if (pos != text.size()) {
        return invalid(text, "trailing characters");
    }

    const auto local = sys_days{ymd} + hours{*h} + minutes{*mi} + seconds{*s};
    return Timestamp{local - offset} + fraction;
}

} // namespace f2c::core
