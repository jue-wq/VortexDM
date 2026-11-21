#include "core/download/retry_policy.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>

TEST_CASE("Retry policy exponentially backs off and caps delay") {
    using namespace std::chrono_literals;
    using vortex::RetryPolicy;

    CHECK(RetryPolicy::decide(0, 3, 503).delay == 500ms);
    CHECK(RetryPolicy::decide(1, 3, 503).delay == 1000ms);
    CHECK(RetryPolicy::decide(2, 3, 503).delay == 2000ms);
    CHECK(RetryPolicy::decide(3, 3, 503).retry == false);
    CHECK(RetryPolicy::decide(10, 20, 503).delay == 30s);
}

TEST_CASE("Retry policy honors Retry-After within configured cap") {
    using namespace std::chrono_literals;
    const auto decision = vortex::RetryPolicy::decide(0, 4, 429, 12s);
    CHECK(decision.retry);
    CHECK(decision.delay == 12s);

    const auto capped = vortex::RetryPolicy::decide(0, 4, 503, 90s);
    CHECK(capped.retry);
    CHECK(capped.delay == 30s);
}

TEST_CASE("Retry policy does not retry permanent HTTP errors") {
    CHECK_FALSE(vortex::RetryPolicy::decide(0, 4, 200).retry);
    CHECK_FALSE(vortex::RetryPolicy::decide(0, 4, 301).retry);
    CHECK_FALSE(vortex::RetryPolicy::decide(0, 4, 400).retry);
    CHECK_FALSE(vortex::RetryPolicy::decide(0, 4, 404).retry);
    CHECK(vortex::RetryPolicy::decide(0, 4, 408).retry);
    CHECK(vortex::RetryPolicy::decide(0, 4, 429).retry);
    CHECK(vortex::RetryPolicy::decide(0, 4, 0).retry);
}

TEST_CASE("Retry policy preserves zero Retry-After with a sub-second cap") {
    using namespace std::chrono_literals;
    const auto decision = vortex::RetryPolicy::decide(
        0, 3, 429, 0s, 500ms, 500ms
    );
    CHECK(decision.retry);
    CHECK(decision.delay == 0ms);
}

TEST_CASE("Retry policy safely caps extremely large Retry-After values") {
    using namespace std::chrono_literals;
    const auto decision = vortex::RetryPolicy::decide(
        0, 3, 503, std::chrono::seconds::max(), 500ms, 250ms
    );
    CHECK(decision.retry);
    CHECK(decision.delay == 250ms);
}
