#include "f2c/config/loader.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <functional>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <variant>

namespace f2c::config {

namespace {

using std::chrono::milliseconds;

constexpr std::string_view modbus_tcp_type = "modbus-tcp";

constexpr std::int64_t min_scan_ms = 50;
constexpr std::int64_t max_scan_ms = 3'600'000;
constexpr std::int64_t last_address = 65'535;

// Accepted keys of each mapping (spec § 5.2). Anything else is an error.
constexpr std::array<std::string_view, 3> top_keys{"site", "mqtt", "devices"};
constexpr std::array<std::string_view, 5> mqtt_keys{"uri", "client_id", "qos", "keepalive_s",
                                                    "heartbeat_s"};
constexpr std::array<std::string_view, 5> device_keys{"id", "path", "scan_ms", "driver", "tags"};
constexpr std::array<std::string_view, 8> modbus_driver_keys{
    "type", "host", "port", "unit_id", "timeout_ms", "reconnect", "byte_order", "word_order"};
constexpr std::array<std::string_view, 2> reconnect_keys{"initial_ms", "max_ms"};
constexpr std::array<std::string_view, 9> modbus_tag_keys{
    "name", "scan_ms", "scale", "offset", "area", "address", "type", "byte_order", "word_order"};

template <typename E>
struct Named {
    std::string_view name;
    E value;
};

constexpr std::array<Named<ModbusArea>, 4> area_names{{
    {"coil", ModbusArea::coil},
    {"discrete_input", ModbusArea::discrete_input},
    {"holding", ModbusArea::holding},
    {"input", ModbusArea::input},
}};
constexpr std::array<Named<ModbusType>, 6> type_names{{
    {"bool", ModbusType::boolean},
    {"int16", ModbusType::int16},
    {"uint16", ModbusType::uint16},
    {"int32", ModbusType::int32},
    {"uint32", ModbusType::uint32},
    {"float32", ModbusType::float32},
}};
constexpr std::array<Named<ByteOrder>, 2> byte_order_names{{
    {"big", ByteOrder::big},
    {"little", ByteOrder::little},
}};
constexpr std::array<Named<WordOrder>, 2> word_order_names{{
    {"big", WordOrder::big},
    {"little", WordOrder::little},
}};

template <typename E, std::size_t N>
std::string_view name_of(const std::array<Named<E>, N>& names, E value) {
    const auto* it = std::ranges::find(names, value, &Named<E>::value);
    return it == names.end() ? "?" : it->name;
}

template <typename E, std::size_t N>
std::string list_names(const std::array<Named<E>, N>& names) {
    std::string text;
    for (const auto& named : names) {
        if (!text.empty()) {
            text += ", ";
        }
        text += named.name;
    }
    return text;
}

std::string in_quotes(std::string_view text) {
    std::string result = "'";
    result += text;
    result += '\'';
    return result;
}

std::string member_path(std::string_view path, std::string_view key) {
    std::string result{path};
    if (!result.empty()) {
        result += '.';
    }
    result += key;
    return result;
}

std::string element_path(std::string_view path, std::size_t index) {
    return std::string{path} + '[' + std::to_string(index) + ']';
}

std::string describe(const YAML::Node& node) {
    switch (node.Type()) {
    case YAML::NodeType::Scalar:
        return "a scalar";
    case YAML::NodeType::Sequence:
        return "a sequence";
    case YAML::NodeType::Map:
        return "a mapping";
    case YAML::NodeType::Null:
    case YAML::NodeType::Undefined:
        break;
    }
    return "null";
}

// Levenshtein distance, for "did you mean" suggestions on short keys.
std::size_t edit_distance(std::string_view a, std::string_view b) {
    std::vector<std::size_t> row(b.size() + 1);
    for (std::size_t j = 0; j < row.size(); ++j) {
        row[j] = j;
    }
    for (std::size_t i = 1; i <= a.size(); ++i) {
        std::size_t diagonal = row[0];
        row[0] = i;
        for (std::size_t j = 1; j <= b.size(); ++j) {
            const std::size_t above = row[j];
            const std::size_t substitution = diagonal + (a[i - 1] == b[j - 1] ? 0 : 1);
            row[j] = std::min({above + 1, row[j - 1] + 1, substitution});
            diagonal = above;
        }
    }
    return row[b.size()];
}

std::optional<std::string_view> suggest(std::string_view key,
                                        std::span<const std::string_view> candidates) {
    constexpr std::size_t max_distance = 2;
    std::optional<std::string_view> best;
    std::size_t best_distance = max_distance + 1;
    for (const auto candidate : candidates) {
        const auto distance = edit_distance(key, candidate);
        if (distance < best_distance) {
            best = candidate;
            best_distance = distance;
        }
    }
    return best;
}

bool is_tcp_uri(std::string_view uri) {
    constexpr std::string_view scheme = "tcp://";
    if (!uri.starts_with(scheme)) {
        return false;
    }
    const auto authority = uri.substr(scheme.size());
    const auto colon = authority.rfind(':');
    if (colon == std::string_view::npos || colon == 0 ||
        authority.find('/') != std::string_view::npos) {
        return false;
    }
    const auto port_text = authority.substr(colon + 1);
    int port = 0;
    const auto* end = port_text.data() + port_text.size();
    const auto [ptr, ec] = std::from_chars(port_text.data(), end, port);
    return ec == std::errc{} && ptr == end && !port_text.empty() && port >= 1 && port <= 65'535;
}

class Context {
public:
    explicit Context(std::string_view source) : source_(source) {}

    [[nodiscard]] std::string location(const YAML::Mark& mark) const {
        std::string result{source_};
        if (!mark.is_null()) {
            result += ':' + std::to_string(mark.line + 1) + ':' + std::to_string(mark.column + 1);
        }
        return result;
    }
    [[nodiscard]] std::string location(const YAML::Node& node) const {
        return location(node.Mark());
    }

    void error(const YAML::Node& at, std::string key_path, std::string message) {
        errors_.push_back({location(at), std::move(key_path), std::move(message)});
    }
    void warning(const YAML::Node& at, std::string key_path, std::string message) {
        warnings_.push_back({location(at), std::move(key_path), std::move(message)});
    }
    void syntax_error(const YAML::Mark& mark, std::string message) {
        errors_.push_back({location(mark), "", std::move(message)});
    }

    [[nodiscard]] std::size_t error_count() const noexcept { return errors_.size(); }
    [[nodiscard]] std::vector<ConfigError> take_errors() { return std::move(errors_); }
    [[nodiscard]] std::vector<ConfigError> take_warnings() { return std::move(warnings_); }

private:
    std::string_view source_;
    std::vector<ConfigError> errors_;
    std::vector<ConfigError> warnings_;
};

enum class Presence : std::uint8_t { required, optional };

// Reads one YAML mapping: checks its keys and converts its values, reporting every problem to the
// Context. Accessors return nullopt when the key is absent or its value is invalid (the error is
// already reported); callers fill defaults with value_or.
class MapReader {
public:
    MapReader(Context& ctx, const YAML::Node& node, std::string path)
        : ctx_(&ctx), node_(node), path_(std::move(path)) {
        if (!node.IsMap()) {
            ctx.error(node, path_, "expected a mapping, got " + describe(node));
            return;
        }
        ok_ = true;
        for (const auto& pair : node) {
            if (!pair.first.IsScalar()) {
                ctx.error(pair.first, path_, "keys must be strings, got " + describe(pair.first));
                continue;
            }
            entries_.push_back({pair.first.Scalar(), pair.first, pair.second});
        }
    }

    [[nodiscard]] bool ok() const noexcept { return ok_; }
    [[nodiscard]] const YAML::Node& node() const noexcept { return node_; }
    [[nodiscard]] std::string path_of(std::string_view key) const {
        return member_path(path_, key);
    }

    // Reports keys outside `allowed` (with a suggestion when one is close) and repeated keys.
    void check_keys(std::span<const std::string_view> allowed) {
        for (std::size_t i = 0; i < entries_.size(); ++i) {
            const auto& entry = entries_[i];
            const auto first = std::ranges::find(entries_, entry.key, &Entry::key);
            if (first != entries_.begin() + static_cast<std::ptrdiff_t>(i)) {
                ctx_->error(entry.key_node, path_of(entry.key),
                            "duplicate key " + in_quotes(entry.key) + " (first at " +
                                ctx_->location(first->key_node) + ")");
                continue;
            }
            if (std::ranges::find(allowed, entry.key) != allowed.end()) {
                continue;
            }
            std::string message = "unknown key " + in_quotes(entry.key);
            if (const auto suggestion = suggest(entry.key, allowed)) {
                message += "; did you mean " + in_quotes(*suggestion) + "?";
                suggested_.emplace_back(*suggestion);
            }
            ctx_->error(entry.key_node, path_of(entry.key), std::move(message));
        }
    }

    [[nodiscard]] std::optional<YAML::Node> find(std::string_view key) const {
        const auto it = std::ranges::find(entries_, key, &Entry::key);
        if (it == entries_.end()) {
            return std::nullopt;
        }
        return it->value;
    }

    [[nodiscard]] std::optional<YAML::Node> get(std::string_view key, Presence presence) {
        auto value = find(key);
        // A misspelled required key is reported once, as unknown with a suggestion.
        if (!value && presence == Presence::required &&
            std::ranges::find(suggested_, key) == suggested_.end()) {
            ctx_->error(node_, path_of(key), "missing required key " + in_quotes(key));
        }
        return value;
    }

    // Reports an error located at the value of `key`, which must be present.
    void error(std::string_view key, std::string message) {
        ctx_->error(find(key).value_or(node_), path_of(key), std::move(message));
    }

    std::optional<std::string> string(std::string_view key, Presence presence) {
        const auto value = get(key, presence);
        if (!value) {
            return std::nullopt;
        }
        if (!value->IsScalar()) {
            error(key, "expected a string, got " + describe(*value));
            return std::nullopt;
        }
        return value->Scalar();
    }

    // A string that must be a valid TagPath segment.
    std::optional<std::string> segment(std::string_view key) {
        auto text = string(key, Presence::required);
        if (text && !core::is_valid_segment(*text)) {
            error(key, in_quotes(*text) + " is not a valid path segment: must match "
                                          "[a-z0-9][a-z0-9_-]{0,63}");
            return std::nullopt;
        }
        return text;
    }

    std::optional<std::int64_t> integer(std::string_view key, Presence presence, std::int64_t min,
                                        std::int64_t max) {
        const auto value = get(key, presence);
        if (!value) {
            return std::nullopt;
        }
        if (!value->IsScalar()) {
            error(key, "expected an integer, got " + describe(*value));
            return std::nullopt;
        }
        if (value->Tag() == "!") {
            error(key, "expected an integer, got a quoted string");
            return std::nullopt;
        }
        const auto& text = value->Scalar();
        std::string_view digits = text;
        if (digits.starts_with('+')) {
            digits.remove_prefix(1);
        }
        std::int64_t result = 0;
        const auto* end = digits.data() + digits.size();
        const auto [ptr, ec] = std::from_chars(digits.data(), end, result);
        if (ec != std::errc{} || ptr != end || digits.starts_with("-") != text.starts_with("-")) {
            error(key, "expected an integer, got " + in_quotes(text));
            return std::nullopt;
        }
        if (result < min || result > max) {
            error(key, "must be between " + std::to_string(min) + " and " + std::to_string(max) +
                           " (got " + std::to_string(result) + ")");
            return std::nullopt;
        }
        return result;
    }

    // A finite floating-point number.
    std::optional<double> number(std::string_view key) {
        const auto value = get(key, Presence::optional);
        if (!value) {
            return std::nullopt;
        }
        if (!value->IsScalar() || value->Tag() == "!") {
            error(key, "expected a number, got " +
                           (value->IsScalar() ? std::string{"a quoted string"} : describe(*value)));
            return std::nullopt;
        }
        const auto& text = value->Scalar();
        std::string_view digits = text;
        if (digits.starts_with('+')) {
            digits.remove_prefix(1);
        }
        double result = 0.0;
        const auto* end = digits.data() + digits.size();
        const auto [ptr, ec] = std::from_chars(digits.data(), end, result);
        if (ec != std::errc{} || ptr != end || digits.starts_with("-") != text.starts_with("-")) {
            error(key, "expected a number, got " + in_quotes(text));
            return std::nullopt;
        }
        if (!std::isfinite(result)) {
            error(key, "must be finite (got " + in_quotes(text) + ")");
            return std::nullopt;
        }
        return result;
    }

    template <typename E, std::size_t N>
    std::optional<E> enumeration(std::string_view key, Presence presence,
                                 const std::array<Named<E>, N>& names) {
        const auto text = string(key, presence);
        if (!text) {
            return std::nullopt;
        }
        const auto* it = std::ranges::find(names, *text, &Named<E>::name);
        if (it == names.end()) {
            error(key, "unknown " + std::string{key} + " " + in_quotes(*text) +
                           "; expected one of: " + list_names(names));
            return std::nullopt;
        }
        return it->value;
    }

    // A sequence with at least one element.
    std::optional<YAML::Node> list(std::string_view key, std::string_view element_name) {
        auto value = get(key, Presence::required);
        if (!value) {
            return std::nullopt;
        }
        if (!value->IsSequence()) {
            error(key, "expected a sequence, got " + describe(*value));
            return std::nullopt;
        }
        if (value->size() == 0) {
            error(key, "at least one " + std::string{element_name} + " is required");
            return std::nullopt;
        }
        return value;
    }

private:
    struct Entry {
        std::string key;
        YAML::Node key_node;
        YAML::Node value;
    };

    Context* ctx_;
    YAML::Node node_;
    std::string path_;
    std::vector<Entry> entries_;
    std::vector<std::string_view> suggested_; // keys offered by check_keys as "did you mean"
    bool ok_{false};
};

// First occurrence of each value of a key that must be unique (device ids, device paths, tag
// names), for "duplicate" errors that point back at the original.
using Seen = std::map<std::string, std::string, std::less<>>;

bool check_unique(Context& ctx, MapReader& map, Seen& seen, std::string_view key,
                  const std::string& value, std::string_view what) {
    const auto location = ctx.location(map.find(key).value_or(map.node()));
    const auto [it, inserted] = seen.emplace(value, location);
    if (!inserted) {
        map.error(key, "duplicate " + std::string{what} + " " + in_quotes(value) + " (first at " +
                           it->second + ")");
    }
    return inserted;
}

std::optional<MqttConfig> parse_mqtt(Context& ctx, const YAML::Node& node, std::string path) {
    const auto errors_before = ctx.error_count();
    MapReader map(ctx, node, std::move(path));
    if (!map.ok()) {
        return std::nullopt;
    }
    map.check_keys(mqtt_keys);

    MqttConfig mqtt;
    if (auto uri = map.string("uri", Presence::required)) {
        if (is_tcp_uri(*uri)) {
            mqtt.uri = std::move(*uri);
        } else {
            map.error("uri",
                      in_quotes(*uri) + " is not a tcp://host:port URI (TLS is out of scope)");
        }
    }
    if (auto client_id = map.segment("client_id")) {
        mqtt.client_id = std::move(*client_id);
    }
    if (const auto qos = map.integer("qos", Presence::optional, 0, 2)) {
        mqtt.qos = static_cast<int>(*qos);
    }
    if (const auto keepalive = map.integer("keepalive_s", Presence::optional, 1, 3600)) {
        mqtt.keepalive = std::chrono::seconds{*keepalive};
    }
    if (const auto heartbeat = map.integer("heartbeat_s", Presence::optional, 0, 86'400)) {
        mqtt.heartbeat = std::chrono::seconds{*heartbeat};
    }

    if (ctx.error_count() != errors_before) {
        return std::nullopt;
    }
    return mqtt;
}

void parse_reconnect(Context& ctx, const YAML::Node& node, std::string path,
                     core::BackoffPolicy& policy) {
    MapReader map(ctx, node, std::move(path));
    if (!map.ok()) {
        return;
    }
    map.check_keys(reconnect_keys);

    constexpr std::int64_t min_ms = 10;
    constexpr std::int64_t max_ms = 600'000;
    const auto initial = map.integer("initial_ms", Presence::optional, min_ms, max_ms);
    const auto max = map.integer("max_ms", Presence::optional, min_ms, max_ms);
    if (initial) {
        policy.initial = milliseconds{*initial};
    }
    if (max) {
        policy.max = milliseconds{*max};
    }
    const bool initial_valid = initial || !map.find("initial_ms");
    const bool max_valid = max || !map.find("max_ms");
    if (initial_valid && max_valid && policy.initial > policy.max) {
        const auto values = " (" + std::to_string(policy.initial.count()) + " > " +
                            std::to_string(policy.max.count()) + ")";
        if (initial) {
            map.error("initial_ms", "must not exceed max_ms" + values);
        } else {
            map.error("max_ms", "must not be less than initial_ms" + values);
        }
    }
}

// Best effort: always returns a config (with defaults where values are invalid) so the device's
// tags can still be checked against it.
ModbusTcpConfig parse_modbus_tcp(Context& ctx, MapReader& map) {
    map.check_keys(modbus_driver_keys);

    ModbusTcpConfig config;
    if (auto host = map.string("host", Presence::required)) {
        if (host->empty()) {
            map.error("host", "must not be empty");
        } else {
            config.host = std::move(*host);
        }
    }
    if (const auto port = map.integer("port", Presence::optional, 1, 65'535)) {
        config.port = static_cast<std::uint16_t>(*port);
    }
    if (const auto unit_id = map.integer("unit_id", Presence::optional, 0, 255)) {
        config.unit_id = static_cast<std::uint8_t>(*unit_id);
    }
    if (const auto timeout = map.integer("timeout_ms", Presence::optional, 10, 60'000)) {
        config.timeout = milliseconds{*timeout};
    }
    if (const auto reconnect = map.find("reconnect")) {
        parse_reconnect(ctx, *reconnect, map.path_of("reconnect"), config.reconnect);
    }
    config.byte_order = map.enumeration("byte_order", Presence::optional, byte_order_names)
                            .value_or(config.byte_order);
    config.word_order = map.enumeration("word_order", Presence::optional, word_order_names)
                            .value_or(config.word_order);
    return config;
}

// nullopt when the driver type is missing or unknown: its other keys cannot be checked then.
std::optional<DriverConfig> parse_driver(Context& ctx, const YAML::Node& node, std::string path) {
    MapReader map(ctx, node, std::move(path));
    if (!map.ok()) {
        return std::nullopt;
    }
    const auto type = map.string("type", Presence::required);
    if (!type) {
        return std::nullopt;
    }
    if (*type == modbus_tcp_type) {
        return parse_modbus_tcp(ctx, map);
    }
    map.error("type", "unknown driver type " + in_quotes(*type) +
                          "; expected one of: " + std::string{modbus_tcp_type});
    return std::nullopt;
}

constexpr bool is_bit_area(ModbusArea area) noexcept {
    return area == ModbusArea::coil || area == ModbusArea::discrete_input;
}

constexpr std::int64_t register_count(ModbusType type) noexcept {
    switch (type) {
    case ModbusType::int32:
    case ModbusType::uint32:
    case ModbusType::float32:
        return 2;
    case ModbusType::boolean:
    case ModbusType::int16:
    case ModbusType::uint16:
        break;
    }
    return 1;
}

std::optional<TagAddress> parse_address(MapReader& map, const ModbusTcpConfig& device) {
    const auto area = map.enumeration("area", Presence::required, area_names);
    const auto address = map.integer("address", Presence::required, 0, last_address);
    const auto type = map.enumeration("type", Presence::required, type_names);
    const auto byte_order = map.enumeration("byte_order", Presence::optional, byte_order_names);
    const auto word_order = map.enumeration("word_order", Presence::optional, word_order_names);
    if (!area || !address || !type) {
        return std::nullopt;
    }

    if (is_bit_area(*area) != (*type == ModbusType::boolean)) {
        map.error("type", "type " + in_quotes(name_of(type_names, *type)) +
                              " is not valid for area " + in_quotes(name_of(area_names, *area)) +
                              "; coil and discrete_input take bool, holding and input take "
                              "int16, uint16, int32, uint32 or float32");
        return std::nullopt;
    }
    const auto end = *address + register_count(*type) - 1;
    if (end > last_address) {
        map.error("address", "a " + std::string{name_of(type_names, *type)} + " at address " +
                                 std::to_string(*address) + " ends at " + std::to_string(end) +
                                 ", past the last address " + std::to_string(last_address));
        return std::nullopt;
    }
    if ((map.find("byte_order") && !byte_order) || (map.find("word_order") && !word_order)) {
        return std::nullopt;
    }
    return ModbusAddress{
        .area = *area,
        .address = static_cast<std::uint16_t>(*address),
        .type = *type,
        .byte_order = byte_order.value_or(device.byte_order),
        .word_order = word_order.value_or(device.word_order),
    };
}

bool is_boolean(const TagAddress& address) {
    struct Visitor {
        bool operator()(const ModbusAddress& a) const noexcept {
            return a.type == ModbusType::boolean;
        }
    };
    return std::visit(Visitor{}, address);
}

std::span<const std::string_view> tag_keys(const DriverConfig& driver) {
    struct Visitor {
        std::span<const std::string_view> operator()(const ModbusTcpConfig& /*config*/) const {
            return modbus_tag_keys;
        }
    };
    return std::visit(Visitor{}, driver);
}

struct DeviceDefaults {
    const core::TagPath* path; // nullptr if the device path is invalid
    milliseconds scan_period;
    const DriverConfig* driver; // nullptr if the driver type is missing or unknown
};

std::optional<TagConfig> parse_tag(Context& ctx, const YAML::Node& node, std::string path,
                                   const DeviceDefaults& device, Seen& names) {
    const auto errors_before = ctx.error_count();
    MapReader map(ctx, node, std::move(path));
    if (!map.ok()) {
        return std::nullopt;
    }
    if (device.driver != nullptr) {
        map.check_keys(tag_keys(*device.driver));
    }

    auto name = map.segment("name");
    std::optional<core::TagPath> tag_path;
    if (name && check_unique(ctx, map, names, "name", *name, "tag name") &&
        device.path != nullptr) {
        if (auto full = device.path->append(*name)) {
            tag_path = std::move(*full);
        } else {
            map.error("name", full.error().message);
        }
    }

    auto scan_period = device.scan_period;
    if (const auto scan = map.integer("scan_ms", Presence::optional, min_scan_ms, max_scan_ms)) {
        scan_period = milliseconds{*scan};
    }

    std::optional<TagAddress> address;
    if (device.driver != nullptr) {
        address =
            std::visit([&map](const ModbusTcpConfig& config) { return parse_address(map, config); },
                       *device.driver);
    }

    std::optional<LinearScaling> scaling;
    if (address && is_boolean(*address)) {
        for (const std::string_view key : {"scale", "offset"}) {
            if (map.find(key)) {
                map.error(key, "scale and offset apply to numeric types only, not bool");
            }
        }
    } else {
        const auto scale = map.number("scale");
        const auto offset = map.number("offset");
        if (scale && *scale == 0.0) {
            map.error("scale", "must not be 0");
        }
        if (map.find("scale") || map.find("offset")) {
            scaling = LinearScaling{.scale = scale.value_or(1.0), .offset = offset.value_or(0.0)};
        }
    }

    if (ctx.error_count() != errors_before || !name || !tag_path || !address) {
        return std::nullopt;
    }
    return TagConfig{
        .name = std::move(*name),
        .path = std::move(*tag_path),
        .scan_period = scan_period,
        .scaling = scaling,
        .address = *address,
    };
}

// A response timeout at least as long as a scan period delays every scan of a dead device.
void check_timeout(Context& ctx, const YAML::Node& driver_node, std::string driver_path,
                   const ModbusTcpConfig& driver, const std::vector<TagConfig>& tags) {
    const auto fastest = std::ranges::min_element(tags, {}, &TagConfig::scan_period);
    if (fastest == tags.end() || driver.timeout < fastest->scan_period) {
        return;
    }
    const MapReader map(ctx, driver_node, std::move(driver_path));
    ctx.warning(map.find("timeout_ms").value_or(driver_node), map.path_of("timeout_ms"),
                "timeout (" + std::to_string(driver.timeout.count()) +
                    " ms) is not shorter than the smallest scan period (" +
                    std::to_string(fastest->scan_period.count()) +
                    " ms); a stopped device delays every scan");
}

struct PlantSeen {
    Seen ids;
    Seen paths;
};

std::optional<DeviceConfig> parse_device(Context& ctx, const YAML::Node& node, std::string path,
                                         const std::optional<core::TagPath>& site,
                                         PlantSeen& seen) {
    const auto errors_before = ctx.error_count();
    MapReader map(ctx, node, std::move(path));
    if (!map.ok()) {
        return std::nullopt;
    }
    map.check_keys(device_keys);

    auto id = map.segment("id");
    if (id) {
        check_unique(ctx, map, seen.ids, "id", *id, "device id");
    }

    std::optional<core::TagPath> device_path;
    if (const auto text = map.string("path", Presence::required)) {
        auto relative = core::TagPath::parse(*text);
        if (!relative) {
            map.error("path", relative.error().message);
        } else if (site) {
            auto full = site->join(*relative);
            if (!full) {
                map.error("path", full.error().message);
            } else if (check_unique(ctx, map, seen.paths, "path", full->str(), "device path")) {
                device_path = std::move(*full);
            }
        }
    }

    milliseconds scan_period{1000};
    if (const auto scan = map.integer("scan_ms", Presence::optional, min_scan_ms, max_scan_ms)) {
        scan_period = milliseconds{*scan};
    }

    std::optional<DriverConfig> driver;
    const auto driver_node = map.get("driver", Presence::required);
    if (driver_node) {
        driver = parse_driver(ctx, *driver_node, map.path_of("driver"));
    }

    std::vector<TagConfig> tags;
    if (const auto list = map.list("tags", "tag")) {
        const DeviceDefaults defaults{
            .path = device_path ? &*device_path : nullptr,
            .scan_period = scan_period,
            .driver = driver ? &*driver : nullptr,
        };
        Seen names;
        for (std::size_t i = 0; i < list->size(); ++i) {
            if (auto tag = parse_tag(ctx, (*list)[i], element_path(map.path_of("tags"), i),
                                     defaults, names)) {
                tags.push_back(std::move(*tag));
            }
        }
    }

    const auto* modbus = driver ? std::get_if<ModbusTcpConfig>(&*driver) : nullptr;
    if (modbus != nullptr && driver_node) {
        check_timeout(ctx, *driver_node, map.path_of("driver"), *modbus, tags);
    }

    if (ctx.error_count() != errors_before || !id || !device_path || !driver) {
        return std::nullopt;
    }
    return DeviceConfig{
        .id = std::move(*id),
        .path = std::move(*device_path),
        .driver = std::move(*driver),
        .tags = std::move(tags),
    };
}

std::optional<PlantConfig> parse_plant(Context& ctx, const YAML::Node& root) {
    MapReader map(ctx, root, "");
    if (!map.ok()) {
        return std::nullopt;
    }
    map.check_keys(top_keys);

    auto site = map.segment("site");
    std::optional<core::TagPath> site_path;
    if (site) {
        if (auto parsed = core::TagPath::parse(*site)) {
            site_path = std::move(*parsed);
        }
    }

    std::optional<MqttConfig> mqtt;
    if (const auto node = map.get("mqtt", Presence::required)) {
        mqtt = parse_mqtt(ctx, *node, map.path_of("mqtt"));
    }

    std::vector<DeviceConfig> devices;
    if (const auto list = map.list("devices", "device")) {
        PlantSeen seen;
        for (std::size_t i = 0; i < list->size(); ++i) {
            if (auto device =
                    parse_device(ctx, (*list)[i], element_path("devices", i), site_path, seen)) {
                devices.push_back(std::move(*device));
            }
        }
    }

    if (ctx.error_count() != 0 || !site || !mqtt) {
        return std::nullopt;
    }
    return PlantConfig{
        .site = std::move(*site), .mqtt = std::move(*mqtt), .devices = std::move(devices)};
}

} // namespace

LoadResult load_plant_config(const std::filesystem::path& file) {
    const auto fail = [&file](std::string message) {
        return tl::unexpected(std::vector<ConfigError>{{file.string(), "", std::move(message)}});
    };

    std::error_code ec;
    const auto status = std::filesystem::status(file, ec);
    if (status.type() == std::filesystem::file_type::not_found) {
        return fail("file not found");
    }
    if (ec) {
        return fail("cannot access file: " + ec.message());
    }
    if (!std::filesystem::is_regular_file(status)) {
        return fail("not a regular file");
    }

    const std::ifstream in(file, std::ios::binary);
    std::ostringstream text;
    if (!in || !(text << in.rdbuf())) {
        return fail("cannot read file");
    }
    return parse_plant_config(text.str(), file.string());
}

LoadResult parse_plant_config(std::string_view yaml, std::string_view source_name) {
    Context ctx(source_name);
    std::optional<PlantConfig> plant;
    try {
        const auto root = YAML::Load(std::string{yaml});
        plant = parse_plant(ctx, root);
    } catch (const YAML::Exception& e) {
        // Syntax errors; yaml-cpp throws nothing else for the calls made above.
        ctx.syntax_error(e.mark, "YAML syntax error: " + e.msg);
    }
    if (!plant || ctx.error_count() != 0) {
        return tl::unexpected(ctx.take_errors());
    }
    return LoadedConfig{.plant = std::move(*plant), .warnings = ctx.take_warnings()};
}

std::string format_errors(std::span<const ConfigError> errors) {
    std::string text;
    for (const auto& error : errors) {
        bool first = true;
        for (const auto* part : {&error.location, &error.key_path, &error.message}) {
            if (part->empty()) {
                continue;
            }
            if (!first) {
                text += ": ";
            }
            text += *part;
            first = false;
        }
        text += '\n';
    }
    return text;
}

} // namespace f2c::config
