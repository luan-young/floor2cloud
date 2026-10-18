// Proves the toolchain works end to end: vcpkg resolves Catch2, the warnings target applies,
// and CTest discovers and runs the test. Removed once libs/core brings real tests (task 3).
#include <catch2/catch_test_macros.hpp>

#include <string_view>

namespace {

constexpr std::string_view project_name() noexcept {
    return "floor2cloud";
}

} // namespace

TEST_CASE("toolchain builds and runs a Catch2 test", "[smoke]") {
    STATIC_REQUIRE(__cplusplus >= 202002L);
    REQUIRE(project_name() == "floor2cloud");
}
