#pragma once

#include "f2c/core/error.hpp"
#include "f2c/core/tag_path.hpp"

#include <string>
#include <string_view>

namespace f2c::messaging {

// MQTT topics of the provisional JSON contract (spec § 6.1, ADR-0008).
inline constexpr std::string_view topic_prefix = "f2c/v1";

[[nodiscard]] std::string tag_topic(const core::TagPath& path); // f2c/v1/<path>
[[nodiscard]] core::Expected<core::TagPath> tag_path_from_topic(std::string_view topic);
[[nodiscard]] bool is_system_topic(std::string_view topic) noexcept; // any segment starts with '_'
[[nodiscard]] std::string gateway_status_topic(std::string_view site, std::string_view client_id);
[[nodiscard]] std::string historian_subscription(); // "f2c/v1/#"

} // namespace f2c::messaging
