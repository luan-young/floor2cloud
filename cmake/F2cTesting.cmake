# Helpers for Catch2 test executables (ADR-0003): one executable per library,
# tests discovered by CTest and labelled `unit` unless told otherwise.
find_package(Catch2 3 CONFIG REQUIRED)
include(Catch)

# f2c_add_test_executable(<name> SOURCES <files...> [LIBRARIES <targets...>] [LABELS <labels...>])
function(f2c_add_test_executable name)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "SOURCES;LIBRARIES;LABELS")
    if(NOT arg_SOURCES)
        message(FATAL_ERROR "f2c_add_test_executable(${name}): SOURCES is required")
    endif()
    if(NOT arg_LABELS)
        set(arg_LABELS unit)
    endif()

    add_executable(${name} ${arg_SOURCES})
    target_link_libraries(${name} PRIVATE ${arg_LIBRARIES} Catch2::Catch2WithMain f2c::warnings)
    catch_discover_tests(${name} PROPERTIES LABELS "${arg_LABELS}")
endfunction()
