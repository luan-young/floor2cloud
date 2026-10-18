#pragma once

#include <tl/expected.hpp> // ADR-0011; becomes std::expected when we move to C++23

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace f2c::core {

enum class Errc : std::uint8_t {
    invalid_argument, // malformed input: tag path, payload field, config value
    parse_error,      // syntax error: YAML, JSON, timestamp
    not_found,        // unknown driver type, missing file
    io_error,         // socket or file I/O failure, connection lost
    timeout,          // no response within the configured time
    protocol_error,   // peer answered with an error (e.g. Modbus exception)
    unavailable,      // dependency not reachable (broker, database)
};

[[nodiscard]] std::string_view to_string(Errc code) noexcept;

struct Error {
    Errc code;
    std::string message; // human-readable, includes context ("filler: read holding 0..4: timeout")
};

template <typename T>
using Expected = tl::expected<T, Error>;

[[nodiscard]] inline tl::unexpected<Error> make_error(Errc code, std::string message) {
    return tl::unexpected<Error>(Error{code, std::move(message)});
}

} // namespace f2c::core
