#pragma once

#include "f2c/config/plant_config.hpp"

#include <tl/expected.hpp>

#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace f2c::config {

struct ConfigError {
    std::string location; // "config/plant1.yaml:14:9"; the file alone if not tied to a node
    std::string key_path; // "devices[0].tags[2].type"; empty for the whole document
    std::string message;  // "unknown type 'uint64'; expected one of: bool, int16, uint16, …"
};

// A valid configuration, plus findings that do not prevent loading (e.g. timeout >= scan period).
struct LoadedConfig {
    PlantConfig plant;
    std::vector<ConfigError> warnings;
};

// All errors are collected; loading never stops at the first one. Never throws.
using LoadResult = tl::expected<LoadedConfig, std::vector<ConfigError>>;

[[nodiscard]] LoadResult load_plant_config(const std::filesystem::path& file);
[[nodiscard]] LoadResult parse_plant_config(std::string_view yaml,
                                            std::string_view source_name = "<string>");

// One "<location>: <key path>: <message>" line per error, each ending in '\n'. Empty parts are
// left out with their separator.
[[nodiscard]] std::string format_errors(std::span<const ConfigError> errors);

} // namespace f2c::config
