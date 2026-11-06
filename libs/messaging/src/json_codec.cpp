#include "f2c/messaging/json_codec.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstddef>
#include <limits>
#include <type_traits>
#include <utility>
#include <variant>

namespace f2c::messaging {

namespace {

using Json = nlohmann::ordered_json; // keeps the key order of the contract examples

std::string dump(const Json& json) {
    // Replace invalid UTF-8 in string values instead of throwing.
    return json.dump(-1, ' ', false, Json::error_handler_t::replace);
}

Json to_json(const core::Value& value) {
    return std::visit(
        [](const auto& v) -> Json {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                return nullptr;
            } else if constexpr (std::is_same_v<T, double>) {
                return std::isfinite(v) ? Json(v) : Json(nullptr);
            } else {
                return v;
            }
        },
        value);
}

// Streaming parser for the sample payload. A SAX handler (not a DOM) because only it sees the
// number's text: nlohmann turns integers beyond 64 bits into doubles, which § 6.2 rejects.
class SampleHandler {
public:
    bool null() { return scalar(core::Value{std::monostate{}}, "null"); }
    bool boolean(bool v) { return scalar(core::Value{v}, "a boolean"); }
    bool number_integer(Json::number_integer_t v) { return scalar(core::Value{v}, "a number"); }

    bool number_unsigned(Json::number_unsigned_t v) {
        if (in_value() &&
            v > static_cast<Json::number_unsigned_t>(std::numeric_limits<std::int64_t>::max())) {
            return reject("'v' is an integer outside the int64 range");
        }
        return scalar(core::Value{static_cast<std::int64_t>(v)}, "a number");
    }

    bool number_float(Json::number_float_t v, const Json::string_t& text) {
        if (in_value() && text.find_first_of(".eE") == Json::string_t::npos) {
            return reject("'v' is an integer outside the int64 range");
        }
        return scalar(core::Value{v}, "a number");
    }

    bool string(Json::string_t& v) {
        if (depth_ == 0) {
            return reject("payload is a string, not an object");
        }
        if (depth_ != 1) {
            return true;
        }
        if (key_ == "v") {
            value_ = core::Value{std::move(v)};
        } else if (key_ == "q") {
            quality_ = std::move(v);
        } else if (key_ == "ts") {
            ts_ = std::move(v);
        } else if (key_ == "gts") {
            gts_ = std::move(v);
        }
        return true;
    }

    static bool binary(Json::binary_t& /*v*/) { return true; } // never produced by the JSON parser

    bool start_object(std::size_t /*size*/) { return open("an object"); }
    bool start_array(std::size_t /*size*/) { return open("an array"); }
    bool end_object() { return close(); }
    bool end_array() { return close(); }

    bool key(Json::string_t& k) {
        if (depth_ == 1) {
            key_ = std::move(k);
        }
        return true;
    }

    bool parse_error(std::size_t /*position*/, const std::string& /*last_token*/,
                     const nlohmann::detail::exception& e) {
        error_ = core::Error{core::Errc::parse_error, e.what()};
        return false;
    }

    // Called after a successful parse.
    core::Expected<core::TagSample> finish(core::TagPath path) {
        if (!value_) {
            return core::make_error(core::Errc::invalid_argument, "missing 'v'");
        }
        if (!quality_) {
            return core::make_error(core::Errc::invalid_argument, "missing or non-string 'q'");
        }
        auto quality = core::parse_quality(*quality_);
        if (!quality) {
            return core::make_error(core::Errc::invalid_argument,
                                    "'q': " + quality.error().message);
        }
        auto source_time = timestamp("ts", ts_);
        if (!source_time) {
            return tl::unexpected(std::move(source_time).error());
        }
        auto gateway_time = timestamp("gts", gts_);
        if (!gateway_time) {
            return tl::unexpected(std::move(gateway_time).error());
        }
        return core::TagSample{std::move(path), std::move(*value_), *quality, *source_time,
                               *gateway_time};
    }

    [[nodiscard]] const std::optional<core::Error>& error() const noexcept { return error_; }

private:
    [[nodiscard]] bool in_value() const noexcept { return depth_ == 1 && key_ == "v"; }

    bool scalar(core::Value value, std::string_view kind) {
        if (depth_ == 0) {
            return reject("payload is " + std::string{kind} + ", not an object");
        }
        if (depth_ == 1) {
            if (key_ == "v") {
                value_ = std::move(value);
            } else if (key_ == "q") {
                quality_.reset(); // non-string quality: reported as missing
            } else if (key_ == "ts" || key_ == "gts") {
                (key_ == "ts" ? ts_ : gts_).reset();
            }
        }
        return true;
    }

    bool open(std::string_view kind) {
        if (depth_ == 0 && kind != "an object") {
            return reject("payload is " + std::string{kind} + ", not an object");
        }
        if (in_value()) {
            return reject("'v' is " + std::string{kind} + ", not a scalar");
        }
        if (depth_ == 1) {
            scalar(core::Value{}, kind); // container under q/ts/gts: same as a non-string
        }
        ++depth_;
        return true;
    }

    bool close() {
        --depth_;
        return true;
    }

    bool reject(std::string message) {
        error_ = core::Error{core::Errc::invalid_argument, std::move(message)};
        return false;
    }

    static core::Expected<core::Timestamp> timestamp(std::string_view key,
                                                     const std::optional<std::string>& text) {
        if (!text) {
            return core::make_error(core::Errc::invalid_argument,
                                    "missing or non-string '" + std::string{key} + "'");
        }
        auto parsed = core::parse_rfc3339(*text);
        if (!parsed) {
            return core::make_error(core::Errc::invalid_argument,
                                    "'" + std::string{key} + "': " + parsed.error().message);
        }
        return parsed;
    }

    int depth_{0};
    std::string key_;
    std::optional<core::Value> value_;
    std::optional<std::string> quality_;
    std::optional<std::string> ts_;
    std::optional<std::string> gts_;
    std::optional<core::Error> error_;
};

} // namespace

std::string encode_sample(const core::TagSample& sample) {
    Json json;
    json["v"] = to_json(sample.value);
    json["q"] = core::to_string(sample.quality);
    json["ts"] = core::format_rfc3339_ms(sample.source_time);
    json["gts"] = core::format_rfc3339_ms(sample.gateway_time);
    return dump(json);
}

core::Expected<core::TagSample> decode_sample(core::TagPath path, std::string_view payload) {
    SampleHandler handler;
    const bool ok = Json::sax_parse(payload, &handler, Json::input_format_t::json, true, false);
    if (!ok) {
        if (const auto& error = handler.error()) {
            return tl::unexpected(*error);
        }
        return core::make_error(core::Errc::parse_error, "malformed JSON");
    }
    return handler.finish(std::move(path));
}

std::string encode_status(GatewayState state, std::optional<core::Timestamp> ts) {
    Json json;
    json["state"] = state == GatewayState::online ? "online" : "offline";
    if (ts) {
        json["ts"] = core::format_rfc3339_ms(*ts);
    }
    json["version"] = F2C_VERSION;
    return dump(json);
}

} // namespace f2c::messaging
