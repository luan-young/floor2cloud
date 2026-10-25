# f2c::drivers_modbus: the Modbus TCP driver (spec § 4.4). Pure core so far: read planning and
# register decoding.
add_library(f2c_drivers_modbus
    src/decode.cpp
    src/read_plan.cpp
)
add_library(f2c::drivers_modbus ALIAS f2c_drivers_modbus)
target_include_directories(f2c_drivers_modbus PUBLIC include)
target_link_libraries(f2c_drivers_modbus PUBLIC f2c::drivers PRIVATE f2c::warnings)

if(F2C_BUILD_TESTS)
    add_subdirectory(tests)
endif()
