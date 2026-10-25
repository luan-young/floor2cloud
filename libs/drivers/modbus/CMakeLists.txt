# f2c::drivers_modbus: the Modbus TCP driver (spec § 4.4). Pure core so far: read planning,
# register decoding and the Poller.
add_library(f2c_drivers_modbus
    src/decode.cpp
    src/poller.cpp
    src/read_plan.cpp
)
add_library(f2c::drivers_modbus ALIAS f2c_drivers_modbus)
target_include_directories(f2c_drivers_modbus PUBLIC include)
target_link_libraries(f2c_drivers_modbus PUBLIC f2c::drivers PRIVATE f2c::warnings)

# Test support (FakeModbusClient), linked only by tests.
add_library(f2c_drivers_modbus_testing testing/src/fake_modbus_client.cpp)
add_library(f2c::drivers_modbus_testing ALIAS f2c_drivers_modbus_testing)
target_include_directories(f2c_drivers_modbus_testing PUBLIC testing/include)
target_link_libraries(f2c_drivers_modbus_testing
    PUBLIC f2c::drivers_modbus f2c::core_testing
    PRIVATE f2c::warnings
)

if(F2C_BUILD_TESTS)
    add_subdirectory(tests)
endif()
