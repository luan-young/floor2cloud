#pragma once

#include "f2c/core/error.hpp"
#include "f2c/core/tag_path.hpp"
#include "f2c/core/tag_sample.hpp"
#include "f2c/core/timestamp.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace f2c::messaging {

// Tag sample payload, spec § 6.2: {"v":…,"q":"good","ts":"…","gts":"…"}.
// A non-finite double (NaN, ±inf) has no JSON form and is written as null.
[[nodiscard]] std::string encode_sample(const core::TagSample& sample);
[[nodiscard]] core::Expected<core::TagSample> decode_sample(core::TagPath path,
                                                            std::string_view payload);

enum class GatewayState : std::uint8_t { online, offline };

// {"state":"online","ts":"…","version":"0.1.0"}; ts omitted when nullopt (the last will).
[[nodiscard]] std::string encode_status(GatewayState state, std::optional<core::Timestamp> ts);

} // namespace f2c::messaging
