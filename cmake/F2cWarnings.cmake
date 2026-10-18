# Shared warning flags (ADR-0002, NF-4). Every f2c target links f2c::warnings.
option(F2C_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)

add_library(f2c_warnings INTERFACE)
add_library(f2c::warnings ALIAS f2c_warnings)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(f2c_warnings INTERFACE
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wshadow
        $<$<BOOL:${F2C_WARNINGS_AS_ERRORS}>:-Werror>
    )
else()
    message(WARNING "f2c_warnings: no warning flags defined for ${CMAKE_CXX_COMPILER_ID}")
endif()
