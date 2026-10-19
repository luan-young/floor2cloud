# f2c::config: plant configuration structs and the YAML loader (spec § 4.2, § 5, ADR-0004).
find_package(yaml-cpp CONFIG REQUIRED)

add_library(f2c_config
    src/loader.cpp
    src/plant_config.cpp
)
add_library(f2c::config ALIAS f2c_config)
target_include_directories(f2c_config PUBLIC include)
# yaml-cpp stays private: nothing downstream reads YAML.
target_link_libraries(f2c_config PUBLIC f2c::core PRIVATE yaml-cpp::yaml-cpp f2c::warnings)

if(F2C_BUILD_TESTS)
    add_subdirectory(tests)
endif()
