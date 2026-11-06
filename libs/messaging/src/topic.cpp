#include "f2c/messaging/topic.hpp"

#include <string>

namespace f2c::messaging {

namespace {

std::string prefixed(std::string_view rest) {
    std::string topic{topic_prefix};
    topic += '/';
    topic += rest;
    return topic;
}

} // namespace

std::string tag_topic(const core::TagPath& path) {
    return prefixed(path.str());
}

core::Expected<core::TagPath> tag_path_from_topic(std::string_view topic) {
    const std::string prefix = prefixed("");
    if (!topic.starts_with(prefix)) {
        return core::make_error(core::Errc::invalid_argument, "topic '" + std::string{topic} +
                                                                  "' does not start with '" +
                                                                  prefix + "'");
    }
    auto path = core::TagPath::parse(topic.substr(prefix.size()));
    if (!path) {
        return core::make_error(core::Errc::invalid_argument,
                                "topic '" + std::string{topic} + "': " + path.error().message);
    }
    return path;
}

bool is_system_topic(std::string_view topic) noexcept {
    return topic.starts_with('_') || topic.find("/_") != std::string_view::npos;
}

std::string gateway_status_topic(std::string_view site, std::string_view client_id) {
    std::string topic = prefixed(site);
    topic += "/_gateway/";
    topic += client_id;
    topic += "/status";
    return topic;
}

std::string historian_subscription() {
    return prefixed("#");
}

} // namespace f2c::messaging
