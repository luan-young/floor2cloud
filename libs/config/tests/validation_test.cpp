// One test per schema rule of spec § 5.2.
#include "f2c/config/loader.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <string>
#include <string_view>

#include "test_documents.hpp"

using Catch::Matchers::ContainsSubstring;
using f2c::config::test::Doc;
using f2c::config::test::errors_of;
using f2c::config::test::must_parse;
using f2c::config::test::scaling_of;
using f2c::config::test::yaml;

namespace {

// Requires exactly one error, at `key_path`, whose message contains `message`.
void check_single_error(const std::string& text, std::string_view key_path,
                        std::string_view message) {
    CAPTURE(text);
    const auto errors = errors_of(text);
    INFO(f2c::config::format_errors(errors));
    REQUIRE(errors.size() == 1);
    CHECK(errors[0].key_path == key_path);
    CHECK_THAT(errors[0].message, ContainsSubstring(std::string{message}));
    CHECK(!errors[0].location.empty()); // not CHECK_FALSE: see errors_of
}

std::string tag(std::string_view fields) {
    return "{ name: t, " + std::string{fields} + " }";
}

} // namespace

TEST_CASE("Missing required keys are errors", "[config][validation]") {
    check_single_error("mqtt: { uri: \"tcp://b:1\", client_id: gw }\n"
                       "devices: [ { id: d, path: a, driver: { type: modbus-tcp, host: h }, "
                       "tags: [ { name: t, area: coil, address: 0, type: bool } ] } ]\n",
                       "site", "missing required key");
    check_single_error(yaml({.tags = {"{ name: t, address: 0, type: uint16 }"}}),
                       "devices[0].tags[0].area", "missing required key");
    check_single_error(yaml({.tags = {"{ area: holding, address: 0, type: uint16 }"}}),
                       "devices[0].tags[0].name", "missing required key");
    check_single_error("site: plant1\nmqtt: { uri: \"tcp://b:1\" }\n"
                       "devices: [ { id: d, path: a, driver: { type: modbus-tcp, host: h }, "
                       "tags: [ { name: t, area: coil, address: 0, type: bool } ] } ]\n",
                       "mqtt.client_id", "missing required key");
    check_single_error("site: plant1\nmqtt: { uri: \"tcp://b:1\", client_id: gw }\n"
                       "devices: [ { id: d, path: a, driver: { host: h }, "
                       "tags: [ { name: t, area: coil, address: 0, type: bool } ] } ]\n",
                       "devices[0].driver.type", "missing required key");
}

TEST_CASE("Required lists must not be empty", "[config][validation]") {
    check_single_error("site: plant1\nmqtt: { uri: \"tcp://b:1\", client_id: gw }\ndevices: []\n",
                       "devices", "at least one");
    check_single_error(yaml({.tags = {}}), "devices[0].tags", "at least one");
}

TEST_CASE("Out-of-range values are errors", "[config][validation]") {
    struct Case {
        Doc doc;
        std::string_view key_path;
    };
    auto c = GENERATE(
        Case{{.mqtt = {"qos: 3"}}, "mqtt.qos"}, Case{{.mqtt = {"qos: -1"}}, "mqtt.qos"},
        Case{{.mqtt = {"keepalive_s: 0"}}, "mqtt.keepalive_s"},
        Case{{.mqtt = {"keepalive_s: 3601"}}, "mqtt.keepalive_s"},
        Case{{.mqtt = {"heartbeat_s: 86401"}}, "mqtt.heartbeat_s"},
        Case{{.device = {"scan_ms: 49"}}, "devices[0].scan_ms"},
        Case{{.device = {"scan_ms: 3600001"}}, "devices[0].scan_ms"},
        Case{{.driver = {"port: 0"}}, "devices[0].driver.port"},
        Case{{.driver = {"port: 65536"}}, "devices[0].driver.port"},
        Case{{.driver = {"unit_id: 256"}}, "devices[0].driver.unit_id"},
        Case{{.driver = {"timeout_ms: 9"}}, "devices[0].driver.timeout_ms"},
        Case{{.driver = {"timeout_ms: 60001"}}, "devices[0].driver.timeout_ms"},
        Case{{.driver = {"reconnect: { initial_ms: 9 }"}},
             "devices[0].driver.reconnect.initial_ms"},
        Case{{.driver = {"reconnect: { max_ms: 600001 }"}}, "devices[0].driver.reconnect.max_ms"},
        Case{{.tags = {tag("area: holding, address: 65536, type: uint16")}},
             "devices[0].tags[0].address"},
        Case{{.tags = {tag("area: holding, address: -1, type: uint16")}},
             "devices[0].tags[0].address"},
        Case{{.tags = {tag("area: holding, address: 0, type: uint16, scan_ms: 10")}},
             "devices[0].tags[0].scan_ms"});
    CAPTURE(c.key_path);

    check_single_error(yaml(c.doc), c.key_path, "must be between");
}

TEST_CASE("Boundary values are accepted", "[config][validation]") {
    const auto loaded = must_parse(yaml({
        .mqtt = {"qos: 0", "keepalive_s: 3600", "heartbeat_s: 0"},
        .device = {"scan_ms: 50"},
        .driver = {"port: 65535", "unit_id: 0", "timeout_ms: 10",
                   "reconnect: { initial_ms: 600000, max_ms: 600000 }"},
        .tags = {tag("area: holding, address: 65535, type: uint16, scan_ms: 3600000")},
    }));

    CHECK(loaded.plant.mqtt.heartbeat.count() == 0);
    CHECK(loaded.plant.devices.at(0).tags.at(0).address.index() == 0);
}

TEST_CASE("Reconnect initial delay must not exceed the maximum", "[config][validation]") {
    check_single_error(yaml({.driver = {"reconnect: { initial_ms: 2000, max_ms: 1000 }"}}),
                       "devices[0].driver.reconnect.initial_ms", "must not exceed max_ms");
    // The default max (10 s) applies when only initial is given.
    check_single_error(yaml({.driver = {"reconnect: { initial_ms: 20000 }"}}),
                       "devices[0].driver.reconnect.initial_ms", "must not exceed max_ms");
}

TEST_CASE("Values of the wrong type are errors", "[config][validation]") {
    check_single_error(yaml({.driver = {"port: \"abc\""}}), "devices[0].driver.port",
                       "expected an integer");
    check_single_error(yaml({.driver = {"port: abc"}}), "devices[0].driver.port",
                       "expected an integer, got 'abc'");
    check_single_error(yaml({.driver = {"port: 1.5"}}), "devices[0].driver.port",
                       "expected an integer");
    check_single_error(yaml({.driver = {"port: \"502\""}}), "devices[0].driver.port",
                       "expected an integer, got a quoted string");
    check_single_error(yaml({.driver = {"port: [502]"}}), "devices[0].driver.port",
                       "expected an integer, got a sequence");
    check_single_error(yaml({.driver = {"port:"}}), "devices[0].driver.port",
                       "expected an integer, got null");
    check_single_error(yaml({.host = "[a, b]"}), "devices[0].driver.host",
                       "expected a string, got a sequence");
    check_single_error(yaml({.mqtt = {"qos: 99999999999999999999"}}), "mqtt.qos",
                       "expected an integer");
    check_single_error(yaml({.tags = {tag("area: input, address: 0, type: int16, scale: x")}}),
                       "devices[0].tags[0].scale", "expected a number");
    check_single_error("site: plant1\nmqtt: tcp://b:1\ndevices: [ { id: d, path: a, driver: "
                       "{ type: modbus-tcp, host: h }, tags: [ { name: t, area: coil, address: "
                       "0, type: bool } ] } ]\n",
                       "mqtt", "expected a mapping, got a scalar");
    check_single_error("site: plant1\nmqtt: { uri: \"tcp://b:1\", client_id: gw }\n"
                       "devices: { id: d }\n",
                       "devices", "expected a sequence, got a mapping");
}

TEST_CASE("Plain numbers in YAML forms are accepted", "[config][validation]") {
    const auto loaded = must_parse(yaml({
        .driver = {"port: +502"},
        .tags = {tag("area: input, address: 0, type: int16, scale: 1e-1, offset: -.5")},
    }));
    const auto& tag0 = loaded.plant.devices.at(0).tags.at(0);

    CHECK(scaling_of(tag0).scale == 0.1);
    CHECK(scaling_of(tag0).offset == -0.5);
}

TEST_CASE("Invalid path segments are errors", "[config][validation]") {
    check_single_error(yaml({.site = "Plant1"}), "site", "Plant1");
    check_single_error(yaml({.site = "plant/one"}), "site", "plant/one");
    check_single_error(yaml({.client_id = "_gw"}), "mqtt.client_id", "_gw");
    check_single_error(yaml({.tags = {"{ name: Speed, area: holding, address: 0, type: uint16 }"}}),
                       "devices[0].tags[0].name", "Speed");
    check_single_error("site: plant1\nmqtt: { uri: \"tcp://b:1\", client_id: gw }\n"
                       "devices: [ { id: D1, path: a, driver: { type: modbus-tcp, host: h }, "
                       "tags: [ { name: t, area: coil, address: 0, type: bool } ] } ]\n",
                       "devices[0].id", "D1");
    check_single_error("site: plant1\nmqtt: { uri: \"tcp://b:1\", client_id: gw }\n"
                       "devices: [ { id: d, path: a//b, driver: { type: modbus-tcp, host: h }, "
                       "tags: [ { name: t, area: coil, address: 0, type: bool } ] } ]\n",
                       "devices[0].path", "a//b");
}

TEST_CASE("A tag path deeper than 16 segments is an error", "[config][validation]") {
    // plant1 + 14 segments = 15; the tag name makes it 16, which is allowed; 15 + 1 + 1 is not.
    const std::string ok_path = "a/b/c/d/e/f/g/h/i/j/k/l/m/n";
    const auto doc = [](const std::string& path) {
        return "site: plant1\nmqtt: { uri: \"tcp://b:1\", client_id: gw }\n"
               "devices: [ { id: d, path: " +
               path +
               ", driver: { type: modbus-tcp, host: h }, "
               "tags: [ { name: t, area: coil, address: 0, type: bool } ] } ]\n";
    };

    CHECK(must_parse(doc(ok_path)).plant.devices.at(0).tags.at(0).path.depth() == 16);
    check_single_error(doc(ok_path + "/o"), "devices[0].tags[0].name", "16 segments");
}

TEST_CASE("The MQTT URI must be tcp://host:port", "[config][validation]") {
    const auto uri = GENERATE(as<std::string>{}, "broker:1883", "ssl://broker:8883", "tcp://broker",
                              "tcp://:1883", "tcp://broker:", "tcp://broker:0",
                              "tcp://broker:65536", "tcp://broker:x", "tcp://bro/ker:1883", "");
    CAPTURE(uri);
    const auto text = "site: plant1\nmqtt: { uri: \"" + uri +
                      "\", client_id: gw }\n"
                      "devices: [ { id: d, path: a, driver: { type: modbus-tcp, host: h }, "
                      "tags: [ { name: t, area: coil, address: 0, type: bool } ] } ]\n";

    check_single_error(text, "mqtt.uri", "tcp://host:port");
}

TEST_CASE("The driver host must not be empty", "[config][validation]") {
    check_single_error("site: plant1\nmqtt: { uri: \"tcp://b:1\", client_id: gw }\n"
                       "devices: [ { id: d, path: a, driver: { type: modbus-tcp, host: \"\" }, "
                       "tags: [ { name: t, area: coil, address: 0, type: bool } ] } ]\n",
                       "devices[0].driver.host", "must not be empty");
}

TEST_CASE("Duplicate device ids are errors", "[config][validation]") {
    check_single_error(R"(
site: plant1
mqtt: { uri: "tcp://broker:1883", client_id: gw }
devices:
  - { id: d, path: area/a, driver: { type: modbus-tcp, host: h },
      tags: [ { name: t, area: coil, address: 0, type: bool } ] }
  - { id: d, path: area/b, driver: { type: modbus-tcp, host: h },
      tags: [ { name: t, area: coil, address: 0, type: bool } ] }
)",
                       "devices[1].id", "duplicate device id 'd' (first at test.yaml:5:11)");
}

TEST_CASE("Duplicate device paths are errors", "[config][validation]") {
    check_single_error(R"(
site: plant1
mqtt: { uri: "tcp://broker:1883", client_id: gw }
devices:
  - { id: a, path: area/x, driver: { type: modbus-tcp, host: h },
      tags: [ { name: t, area: coil, address: 0, type: bool } ] }
  - { id: b, path: area/x, driver: { type: modbus-tcp, host: h },
      tags: [ { name: t, area: coil, address: 0, type: bool } ] }
)",
                       "devices[1].path", "duplicate device path 'plant1/area/x'");
}

TEST_CASE("Duplicate tag names in a device are errors", "[config][validation]") {
    check_single_error(yaml({.tags = {tag("area: holding, address: 0, type: uint16"),
                                      tag("area: holding, address: 1, type: uint16")}}),
                       "devices[0].tags[1].name", "duplicate tag name 't'");
}

TEST_CASE("Overlapping tags at the same address are allowed", "[config][validation]") {
    const auto loaded = must_parse(yaml({.tags = {
                                             "{ name: whole, area: holding, address: 0, type: "
                                             "uint32 }",
                                             "{ name: low, area: holding, address: 1, type: "
                                             "uint16 }",
                                         }}));

    CHECK(loaded.plant.devices.at(0).tags.size() == 2);
}

TEST_CASE("bool is only valid for bit areas", "[config][validation]") {
    const auto* const area = GENERATE("holding", "input");
    CAPTURE(area);

    check_single_error(
        yaml({.tags = {tag("area: " + std::string{area} + ", address: 0, type: bool")}}),
        "devices[0].tags[0].type", "not valid for area");
}

TEST_CASE("Register types are only valid for register areas", "[config][validation]") {
    const auto* const area = GENERATE("coil", "discrete_input");
    const auto* const type = GENERATE("int16", "uint16", "int32", "uint32", "float32");
    CAPTURE(area, type);

    check_single_error(yaml({.tags = {tag("area: " + std::string{area} +
                                          ", address: 0, type: " + std::string{type})}}),
                       "devices[0].tags[0].type", "not valid for area");
}

TEST_CASE("Every bit area and register type combination loads", "[config][validation]") {
    must_parse(yaml({.tags = {
                         "{ name: a, area: coil, address: 0, type: bool }",
                         "{ name: b, area: discrete_input, address: 0, type: bool }",
                         "{ name: c, area: holding, address: 0, type: int16 }",
                         "{ name: d, area: holding, address: 0, type: uint16 }",
                         "{ name: e, area: input, address: 0, type: int32 }",
                         "{ name: f, area: input, address: 0, type: uint32 }",
                         "{ name: g, area: input, address: 0, type: float32 }",
                     }}));
}

TEST_CASE("Unknown enum values list the accepted ones", "[config][validation]") {
    check_single_error(yaml({.tags = {tag("area: holding, address: 0, type: uint64")}}),
                       "devices[0].tags[0].type",
                       "unknown type 'uint64'; expected one of: bool, int16, uint16, int32, "
                       "uint32, float32");
    check_single_error(yaml({.driver = {"byte_order: middle"}}), "devices[0].driver.byte_order",
                       "expected one of: big, little");
    check_single_error(yaml({.tags = {tag("area: holding, address: 0, type: uint32, "
                                          "word_order: swapped")}}),
                       "devices[0].tags[0].word_order", "expected one of: big, little");
}

TEST_CASE("Scaling is only valid for numeric types", "[config][validation]") {
    const auto* const key = GENERATE("scale: 2", "offset: 1");
    CAPTURE(key);

    const auto errors =
        errors_of(yaml({.tags = {tag("area: coil, address: 0, type: bool, " + std::string{key})}}));

    REQUIRE(errors.size() == 1);
    CHECK_THAT(errors[0].key_path, ContainsSubstring("devices[0].tags[0]."));
    CHECK_THAT(errors[0].message, ContainsSubstring("numeric types only"));
}

TEST_CASE("Scale must be finite and non-zero; offset finite", "[config][validation]") {
    check_single_error(yaml({.tags = {tag("area: input, address: 0, type: int16, scale: 0")}}),
                       "devices[0].tags[0].scale", "must not be 0");
    check_single_error(yaml({.tags = {tag("area: input, address: 0, type: int16, scale: inf")}}),
                       "devices[0].tags[0].scale", "must be finite");
    check_single_error(yaml({.tags = {tag("area: input, address: 0, type: int16, offset: nan")}}),
                       "devices[0].tags[0].offset", "must be finite");
}

TEST_CASE("A value must fit below the last address", "[config][validation]") {
    const auto* const type = GENERATE("int32", "uint32", "float32");
    CAPTURE(type);

    check_single_error(
        yaml({.tags = {tag("area: holding, address: 65535, type: " + std::string{type})}}),
        "devices[0].tags[0].address", "65535");
    must_parse(yaml({.tags = {tag("area: holding, address: 65534, type: " + std::string{type})}}));
}

TEST_CASE("Unknown driver types are errors", "[config][validation]") {
    // Driver-specific keys of an unknown driver are not checked: the type error is the one to fix.
    check_single_error(yaml({.driver = {"port: 4840"}, .driver_type = "opcua"}),
                       "devices[0].driver.type",
                       "unknown driver type 'opcua'; expected one of: modbus-tcp");
}

TEST_CASE("Unknown keys are errors with a suggestion when close", "[config][validation]") {
    check_single_error(yaml({.device = {"scan_mss: 100"}}), "devices[0].scan_mss",
                       "unknown key 'scan_mss'; did you mean 'scan_ms'?");
    check_single_error(yaml({.mqtt = {"keepalive: 10"}}), "mqtt.keepalive",
                       "did you mean 'keepalive_s'?");
    check_single_error(yaml({.driver = {"unitid: 3"}}), "devices[0].driver.unitid",
                       "did you mean 'unit_id'?");
    check_single_error(yaml({.tags = {tag("area: holding, adress: 0, type: uint16")}}),
                       "devices[0].tags[0].adress", "did you mean 'address'?");
    check_single_error(yaml({.driver = {"reconnect: { inital_ms: 100 }"}}),
                       "devices[0].driver.reconnect.inital_ms", "did you mean 'initial_ms'?");
    check_single_error(yaml({.top = {"sites: x"}}), "sites", "did you mean 'site'?");

    const auto errors = errors_of(yaml({.mqtt = {"password: secret"}}));
    REQUIRE(errors.size() == 1);
    CHECK(errors[0].message == "unknown key 'password'");
}

TEST_CASE("Duplicate keys in a mapping are errors", "[config][validation]") {
    check_single_error(yaml({.mqtt = {"qos: 0", "qos: 2"}}), "mqtt.qos", "duplicate key 'qos'");
}
