f2c_add_test_executable(f2c_drivers_modbus_tests
    SOURCES
        decode_test.cpp
        poller_test.cpp
        read_plan_test.cpp
        register_test.cpp
    LIBRARIES
        f2c::drivers_modbus
        f2c::drivers_modbus_testing
)
target_compile_definitions(f2c_drivers_modbus_tests PRIVATE
    F2C_PLANT1_YAML="${PROJECT_SOURCE_DIR}/config/plant1.yaml"
)

# LibmodbusClient and ModbusDriver against the simulator's server running in-process on an
# ephemeral loopback port (spec § 9.1): sockets, so not `unit`, but nothing outside the process.
f2c_add_test_executable(f2c_drivers_modbus_component_tests
    SOURCES
        modbus_component_test.cpp
    LIBRARIES
        f2c::drivers_modbus
        f2c::sim_server
    LABELS
        component
)
