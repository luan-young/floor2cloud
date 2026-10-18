# f2c::core: vocabulary types shared by every library (spec § 4.1).
find_package(tl-expected CONFIG REQUIRED)

add_library(f2c_core
    src/backoff.cpp
    src/clock.cpp
    src/error.cpp
    src/tag_path.cpp
    src/tag_sample.cpp
    src/timestamp.cpp
)
add_library(f2c::core ALIAS f2c_core)
target_include_directories(f2c_core PUBLIC include)
target_link_libraries(f2c_core PUBLIC tl::expected PRIVATE f2c::warnings)

# Test support (ManualClock), linked only by tests.
add_library(f2c_core_testing testing/src/manual_clock.cpp)
add_library(f2c::core_testing ALIAS f2c_core_testing)
target_include_directories(f2c_core_testing PUBLIC testing/include)
target_link_libraries(f2c_core_testing PUBLIC f2c::core PRIVATE f2c::warnings)

if(F2C_BUILD_TESTS)
    add_subdirectory(tests)
endif()
