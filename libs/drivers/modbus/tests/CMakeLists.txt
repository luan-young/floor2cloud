f2c_add_test_executable(f2c_drivers_modbus_tests
    SOURCES
        decode_test.cpp
        poller_test.cpp
        read_plan_test.cpp
    LIBRARIES
        f2c::drivers_modbus
        f2c::drivers_modbus_testing
)
target_compile_definitions(f2c_drivers_modbus_tests PRIVATE
    F2C_PLANT1_YAML="${PROJECT_SOURCE_DIR}/config/plant1.yaml"
)
