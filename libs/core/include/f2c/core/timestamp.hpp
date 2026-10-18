#pragma once

#include "f2c/core/error.hpp"

#include <chrono>
#include <string>
#include <string_view>

namespace f2c::core {

using Timestamp = std::chrono::sys_time<std::chrono::nanoseconds>; // always UTC

// "2026-10-20T13:05:01.120Z": exactly three fractional digits, 'Z' suffix, truncated (floor) to ms.
[[nodiscard]] std::string format_rfc3339_ms(Timestamp t);

// Accepts RFC 3339 with 'Z' or a ±hh:mm offset and 0 to 9 fractional digits. Leap seconds
// (":60") are rejected: Timestamp cannot represent them.
[[nodiscard]] Expected<Timestamp> parse_rfc3339(std::string_view text);

} // namespace f2c::core
