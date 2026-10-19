#pragma once

#include "f2c/config/loader.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace f2c::config::test {

// A minimal valid plant file with one device and one tag. Each list adds lines ("key: value") to
// its section; `tags` replaces the tag list, one flow mapping per entry.
struct Doc {
    // Explicit {}: GCC's -Wmissing-field-initializers flags designated initializers that skip
    // members without one.
    std::vector<std::string> top{};    // NOLINT(readability-redundant-member-init)
    std::vector<std::string> mqtt{};   // NOLINT(readability-redundant-member-init)
    std::vector<std::string> device{}; // NOLINT(readability-redundant-member-init)
    std::vector<std::string> driver{}; // NOLINT(readability-redundant-member-init)
    std::vector<std::string> tags{"{ name: t, area: holding, address: 0, type: uint16 }"};
    std::string site{"plant1"};
    std::string client_id{"gw"};
    std::string driver_type{"modbus-tcp"};
    std::string host{"h"};
};

inline std::string yaml(const Doc& doc) {
    std::string text = "site: " + doc.site + "\n";
    for (const auto& line : doc.top) {
        text += line + "\n";
    }
    text += "mqtt:\n  uri: tcp://broker:1883\n  client_id: " + doc.client_id + "\n";
    for (const auto& line : doc.mqtt) {
        text += "  " + line + "\n";
    }
    text += "devices:\n  - id: d1\n    path: area/d1\n";
    for (const auto& line : doc.device) {
        text += "    " + line + "\n";
    }
    text += "    driver:\n      type: " + doc.driver_type + "\n      host: " + doc.host + "\n";
    for (const auto& line : doc.driver) {
        text += "      " + line + "\n";
    }
    text += doc.tags.empty() ? "    tags: []\n" : "    tags:\n";
    for (const auto& tag : doc.tags) {
        text += "      - " + tag + "\n";
    }
    return text;
}

// The scaling of a tag that must have one.
inline LinearScaling scaling_of(const TagConfig& tag) {
    if (!tag.scaling) {
        FAIL("tag '" << tag.name << "' has no scaling");
        return {};
    }
    return *tag.scaling;
}

inline LoadedConfig must_parse(std::string_view text) {
    auto result = parse_plant_config(text, "test.yaml");
    if (!result) {
        FAIL("unexpected errors:\n" << format_errors(result.error()));
    }
    return *std::move(result);
}

inline std::vector<ConfigError> errors_of(std::string_view text) {
    auto result = parse_plant_config(text, "test.yaml");
    REQUIRE(!result.has_value()); // not REQUIRE_FALSE: trips clang-analyzer's EnumCastOutOfRange
    return result.error();
}

} // namespace f2c::config::test
