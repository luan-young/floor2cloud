#include "f2c/core/backoff.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>

using f2c::core::Backoff;
using f2c::core::BackoffPolicy;
using namespace std::chrono_literals;

TEST_CASE("Backoff grows exponentially and caps at max", "[core][backoff]") {
    Backoff backoff{BackoffPolicy{}};

    CHECK(backoff.next_delay() == 500ms);
    CHECK(backoff.next_delay() == 1000ms);
    CHECK(backoff.next_delay() == 2000ms);
    CHECK(backoff.next_delay() == 4000ms);
    CHECK(backoff.next_delay() == 8000ms);
    CHECK(backoff.next_delay() == 10'000ms);
    CHECK(backoff.next_delay() == 10'000ms);
}

TEST_CASE("Backoff reset starts again from initial", "[core][backoff]") {
    Backoff backoff{BackoffPolicy{}};
    (void)backoff.next_delay();
    (void)backoff.next_delay();
    (void)backoff.next_delay();

    backoff.reset();

    CHECK(backoff.next_delay() == 500ms);
    CHECK(backoff.next_delay() == 1000ms);
}

TEST_CASE("Backoff with multiplier 1.0 is constant", "[core][backoff]") {
    Backoff backoff{BackoffPolicy{.initial = 300ms, .max = 10'000ms, .multiplier = 1.0}};

    for (int i = 0; i < 5; ++i) {
        CHECK(backoff.next_delay() == 300ms);
    }
}

TEST_CASE("Backoff with a fractional multiplier", "[core][backoff]") {
    Backoff backoff{BackoffPolicy{.initial = 100ms, .max = 300ms, .multiplier = 1.5}};

    CHECK(backoff.next_delay() == 100ms);
    CHECK(backoff.next_delay() == 150ms);
    CHECK(backoff.next_delay() == 225ms);
    CHECK(backoff.next_delay() == 300ms);
}

TEST_CASE("Backoff with initial equal to max is constant", "[core][backoff]") {
    Backoff backoff{BackoffPolicy{.initial = 1000ms, .max = 1000ms, .multiplier = 2.0}};

    CHECK(backoff.next_delay() == 1000ms);
    CHECK(backoff.next_delay() == 1000ms);
}
