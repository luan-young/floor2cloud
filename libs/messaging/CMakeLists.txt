# f2c::messaging: MQTT topics, JSON payload codec and publish-on-change filter (spec § 4.5, § 6).
find_package(nlohmann_json CONFIG REQUIRED)

add_library(f2c_messaging
    src/change_filter.cpp
    src/json_codec.cpp
    src/topic.cpp
)
add_library(f2c::messaging ALIAS f2c_messaging)
target_include_directories(f2c_messaging PUBLIC include)
target_link_libraries(f2c_messaging PUBLIC f2c::core PRIVATE nlohmann_json::nlohmann_json f2c::warnings)
# The gateway status payload carries the project version (§ 6.4).
target_compile_definitions(f2c_messaging PRIVATE F2C_VERSION="${PROJECT_VERSION}")

if(F2C_BUILD_TESTS)
    add_subdirectory(tests)
endif()
