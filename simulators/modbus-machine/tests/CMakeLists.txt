f2c_add_test_executable(f2c_sim_tests
    SOURCES
        filler_model_test.cpp
        options_test.cpp
        register_map_test.cpp
    LIBRARIES
        f2c::sim
)

# The I/O shell against a real libmodbus client over loopback (spec § 9.3): not `unit`, because it
# opens sockets (NF-5), but needs nothing outside the test process.
f2c_add_test_executable(f2c_sim_component_tests
    SOURCES
        server_component_test.cpp
    LIBRARIES
        f2c::sim_server
        PkgConfig::libmodbus
    LABELS
        component
)
