# f2c::messaging: MQTT topics, JSON payload codec, publish-on-change filter, the MqttClient
# interface with its Paho adapter, and SamplePublisher (spec § 4.5, § 6; ADR-0006).
find_package(asio CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(PahoMqttCpp CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

add_library(f2c_messaging
    src/change_filter.cpp
    src/json_codec.cpp
    src/paho_mqtt_client.cpp
    src/sample_publisher.cpp
    src/topic.cpp
)
add_library(f2c::messaging ALIAS f2c_messaging)
target_include_directories(f2c_messaging PUBLIC include)
# Asio is public: SamplePublisher takes an executor. Paho stays private (ADR-0006).
target_link_libraries(f2c_messaging
    PUBLIC f2c::core asio::asio
    PRIVATE nlohmann_json::nlohmann_json PahoMqttCpp::paho-mqttpp3-static spdlog::spdlog
            f2c::warnings
)
# The gateway status payload carries the project version (§ 6.4).
target_compile_definitions(f2c_messaging PRIVATE F2C_VERSION="${PROJECT_VERSION}")

if(F2C_BUILD_TESTS)
    # Test support (FakeMqttClient, MosquittoBroker), linked only by tests.
    add_library(f2c_messaging_testing
        testing/src/fake_mqtt_client.cpp
        testing/src/mosquitto_broker.cpp
    )
    add_library(f2c::messaging_testing ALIAS f2c_messaging_testing)
    target_include_directories(f2c_messaging_testing PUBLIC testing/include)
    target_link_libraries(f2c_messaging_testing PUBLIC f2c::messaging PRIVATE f2c::warnings)
    # The broker for the PahoMqttClient component tests (ADR-0003). Looked up again on every
    # configure until found; MosquittoBroker reports a missing one when a test starts it.
    find_program(F2C_MOSQUITTO_EXECUTABLE mosquitto PATHS /usr/sbin /usr/local/sbin)
    if(F2C_MOSQUITTO_EXECUTABLE) # false for "" and for F2C_MOSQUITTO_EXECUTABLE-NOTFOUND
        set(f2c_mosquitto "${F2C_MOSQUITTO_EXECUTABLE}")
    else()
        set(f2c_mosquitto "")
        message(WARNING "mosquitto not found: the PahoMqttClient component tests will fail "
                        "(apt-get install mosquitto)")
    endif()
    # Empty when not found.
    target_compile_definitions(f2c_messaging_testing
        PRIVATE F2C_MOSQUITTO_EXECUTABLE="${f2c_mosquitto}"
    )

    add_subdirectory(tests)
endif()
