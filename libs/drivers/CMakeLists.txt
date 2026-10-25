# f2c::drivers: the Driver interface and DriverRegistry (spec § 4.3, ADR-0007).
find_package(asio CONFIG REQUIRED)

add_library(f2c_drivers
    src/registry.cpp
)
add_library(f2c::drivers ALIAS f2c_drivers)
target_include_directories(f2c_drivers PUBLIC include)
# Asio is public: the factory signature carries an executor (ADR-0007, ADR-0010).
target_link_libraries(f2c_drivers PUBLIC f2c::config asio::asio PRIVATE f2c::warnings)

if(F2C_BUILD_TESTS)
    add_subdirectory(tests)
endif()
