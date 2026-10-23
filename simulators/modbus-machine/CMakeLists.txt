# f2c::sim: simulated filling machine served over Modbus TCP (spec § 2.1, § 4.7).
# libmodbus ships a pkg-config module; vcpkg's CMake config for it is unofficial and warns.
find_package(PkgConfig REQUIRED)
pkg_check_modules(libmodbus REQUIRED IMPORTED_TARGET libmodbus)
find_package(spdlog CONFIG REQUIRED)

# Pure part (model, register image, CLI parsing): no I/O, unit-tested.
add_library(f2c_sim
    src/filler_model.cpp
    src/options.cpp
    src/register_map.cpp
)
add_library(f2c::sim ALIAS f2c_sim)
target_include_directories(f2c_sim PUBLIC src)
target_link_libraries(f2c_sim PUBLIC f2c::core PRIVATE f2c::warnings)

# I/O shell: the libmodbus server loop. A library of its own so the Modbus driver's local smoke
# test (task 9) can run the simulator in-process.
add_library(f2c_sim_server src/server.cpp)
add_library(f2c::sim_server ALIAS f2c_sim_server)
target_link_libraries(f2c_sim_server
    PUBLIC f2c::sim
    PRIVATE PkgConfig::libmodbus spdlog::spdlog f2c::warnings
)

add_executable(f2c-modbus-machine src/main.cpp)
target_link_libraries(f2c-modbus-machine PRIVATE f2c::sim_server spdlog::spdlog f2c::warnings)
install(TARGETS f2c-modbus-machine RUNTIME DESTINATION bin)

if(F2C_BUILD_TESTS)
    add_subdirectory(tests)
endif()
