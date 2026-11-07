f2c_add_test_executable(f2c_messaging_tests
    SOURCES
        change_filter_test.cpp
        json_codec_test.cpp
        sample_publisher_test.cpp
        topic_test.cpp
    LIBRARIES
        f2c::messaging
        f2c::messaging_testing
        f2c::core_testing
)
target_compile_definitions(f2c_messaging_tests PRIVATE F2C_VERSION="${PROJECT_VERSION}")

# SamplePublisher's heartbeat timer against the real clock; PahoMqttClient against a Mosquitto
# child process (MosquittoBroker).
f2c_add_test_executable(f2c_messaging_component_tests
    SOURCES
        paho_mqtt_client_component_test.cpp
        sample_publisher_component_test.cpp
    LIBRARIES
        f2c::messaging
        f2c::messaging_testing
    LABELS
        component
)
