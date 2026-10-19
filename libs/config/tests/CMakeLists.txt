f2c_add_test_executable(f2c_config_tests
    SOURCES
        format_errors_test.cpp
        loader_test.cpp
        plant_config_test.cpp
        validation_test.cpp
    LIBRARIES
        f2c::config
)
target_compile_definitions(f2c_config_tests PRIVATE
    F2C_PLANT1_YAML="${PROJECT_SOURCE_DIR}/config/plant1.yaml"
)
