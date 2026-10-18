#include "f2c/core/error.hpp"

namespace f2c::core {

std::string_view to_string(Errc code) noexcept {
    switch (code) {
    case Errc::invalid_argument:
        return "invalid_argument";
    case Errc::parse_error:
        return "parse_error";
    case Errc::not_found:
        return "not_found";
    case Errc::io_error:
        return "io_error";
    case Errc::timeout:
        return "timeout";
    case Errc::protocol_error:
        return "protocol_error";
    case Errc::unavailable:
        return "unavailable";
    }
    return "unknown";
}

} // namespace f2c::core
