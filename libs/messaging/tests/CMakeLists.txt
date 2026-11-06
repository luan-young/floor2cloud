f2c_add_test_executable(f2c_messaging_tests
    SOURCES
        change_filter_test.cpp
        json_codec_test.cpp
        topic_test.cpp
    LIBRARIES
        f2c::messaging
)
target_compile_definitions(f2c_messaging_tests PRIVATE F2C_VERSION="${PROJECT_VERSION}")
