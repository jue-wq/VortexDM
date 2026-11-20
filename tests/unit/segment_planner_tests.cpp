#include "core/download/segment_planner.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>

TEST_CASE("Segment planner partitions file into balanced inclusive ranges") {
    const auto result = vortex::SegmentPlanner::split(10, 3);
    REQUIRE(result);
    REQUIRE(result->size() == 3);
    CHECK((*result)[0].first == 0);
    CHECK((*result)[0].last == 3);
    CHECK((*result)[1].first == 4);
    CHECK((*result)[1].last == 6);
    CHECK((*result)[2].first == 7);
    CHECK((*result)[2].last == 9);

    std::uint64_t total = 0;
    for (std::size_t i = 0; i < result->size(); ++i) {
        total += (*result)[i].size();
        if (i > 0) CHECK((*result)[i - 1].last + 1 == (*result)[i].first);
    }
    CHECK(total == 10);
}

TEST_CASE("Segment planner caps segments to byte count and rejects invalid input") {
    const auto tiny = vortex::SegmentPlanner::split(2, 8);
    REQUIRE(tiny);
    CHECK(tiny->size() == 2);
    CHECK_FALSE(vortex::SegmentPlanner::split(0, 2));
    CHECK_FALSE(vortex::SegmentPlanner::split(100, 17));
    CHECK_FALSE(vortex::SegmentPlanner::split(100, 0));
}
