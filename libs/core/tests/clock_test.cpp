#include "f2c/core/clock.hpp"
#include "f2c/core/testing/manual_clock.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>

using f2c::core::Clock;
using f2c::core::Timestamp;
using f2c::core::testing::ManualClock;
using namespace std::chrono;
using namespace std::chrono_literals;

TEST_CASE("ManualClock starts where it is told", "[core][clock]") {
    const Timestamp start{sys_days{2026y / October / 20}};
    const ManualClock clock{start};

    CHECK(clock.now() == start);
}

TEST_CASE("ManualClock::advance moves both clocks", "[core][clock]") {
    ManualClock clock{Timestamp{sys_days{2026y / October / 20}}};
    const Clock& as_clock = clock;
    const auto wall_before = as_clock.now();
    const auto steady_before = as_clock.steady_now();

    clock.advance(1500ms);

    CHECK(as_clock.now() - wall_before == 1500ms);
    CHECK(as_clock.steady_now() - steady_before == 1500ms);

    clock.advance(1ns);

    CHECK(as_clock.now() - wall_before == 1'500'000'001ns);
    CHECK(as_clock.steady_now() - steady_before == 1'500'000'001ns);
}

TEST_CASE("ManualClock does not move on its own", "[core][clock]") {
    const ManualClock clock;

    CHECK(clock.now() == clock.now());
    CHECK(clock.steady_now() == clock.steady_now());
}

TEST_CASE("system_clock follows the real clocks", "[core][clock]") {
    const Clock& clock = f2c::core::system_clock();

    const auto wall = clock.now();
    const auto steady = clock.steady_now();

    CHECK(abs(wall - time_point_cast<nanoseconds>(system_clock::now())) < 1min);
    CHECK(steady <= steady_clock::now());
    CHECK(&clock == &f2c::core::system_clock());
}
