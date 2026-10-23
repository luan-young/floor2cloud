cmake_minimum_required(VERSION 3.25)

project(floor2cloud
    VERSION 0.1.0
    DESCRIPTION "Reference IIoT platform: shop floor to cloud"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(F2cWarnings)

option(F2C_BUILD_TESTS "Build the unit tests" ON)

if(F2C_BUILD_TESTS)
    enable_testing()
    include(F2cTesting)
endif()

add_subdirectory(libs/core)
add_subdirectory(libs/config)
add_subdirectory(simulators/modbus-machine)
