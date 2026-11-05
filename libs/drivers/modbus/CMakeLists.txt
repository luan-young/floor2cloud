# f2c::drivers_modbus: the Modbus TCP driver (spec § 4.4): read planning, register decoding and
# the Poller (pure core), plus the libmodbus client and the Asio shell.
# libmodbus ships a pkg-config module; vcpkg's CMake config for it is unofficial and warns. GLOBAL,
# so services linking this library from other directories can resolve the target.
find_package(PkgConfig REQUIRED)
pkg_check_modules(libmodbus REQUIRED IMPORTED_TARGET GLOBAL libmodbus)
find_package(spdlog CONFIG REQUIRED)

add_library(f2c_drivers_modbus
    src/decode.cpp
    src/libmodbus_client.cpp
    src/modbus_driver.cpp
    src/poller.cpp
    src/read_plan.cpp
)
add_library(f2c::drivers_modbus ALIAS f2c_drivers_modbus)
target_include_directories(f2c_drivers_modbus PUBLIC include)
target_link_libraries(f2c_drivers_modbus
    PUBLIC f2c::drivers
    PRIVATE PkgConfig::libmodbus spdlog::spdlog f2c::warnings
)

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
