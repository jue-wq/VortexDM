#include "core/download/content_range.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>

TEST_CASE("Content-Range parser accepts valid satisfied byte ranges") {
    const auto parsed = vortex::ContentRangeParser::parse("bytes 100-199/1000");
    REQUIRE(parsed.has_value());
    CHECK(parsed->range.first == 100);
    CHECK(parsed->range.last == 199);
    CHECK(parsed->total_size == 1000);

    const auto spaced = vortex::ContentRangeParser::parse("  BYTES 0-0/1  ");
    REQUIRE(spaced.has_value());
    CHECK(spaced->range.size() == 1);
}

TEST_CASE("Content-Range parser rejects malformed and unsatisfied ranges") {
    CHECK_FALSE(vortex::ContentRangeParser::parse("").has_value());
    CHECK_FALSE(vortex::ContentRangeParser::parse("items 0-1/2").has_value());
    CHECK_FALSE(vortex::ContentRangeParser::parse("bytes 0-1/*").has_value());
    CHECK_FALSE(vortex::ContentRangeParser::parse("bytes */100").has_value());
    CHECK_FALSE(vortex::ContentRangeParser::parse("bytes 2-1/3").has_value());
    CHECK_FALSE(vortex::ContentRangeParser::parse("bytes 0-3/3").has_value());
    CHECK_FALSE(vortex::ContentRangeParser::parse("bytes 0-1/0").has_value());
    CHECK_FALSE(vortex::ContentRangeParser::parse("bytes 0-1/2junk").has_value());
    CHECK_FALSE(vortex::ContentRangeParser::parse("bytes 0-1/2, 4-5/6").has_value());
}

TEST_CASE("Content-Range parser rejects integer overflow") {
    const auto too_large = std::numeric_limits<std::uint64_t>::max();
    const auto header = "bytes 0-" + std::to_string(too_large) + "/10";
    CHECK_FALSE(vortex::ContentRangeParser::parse(header).has_value());
    CHECK_FALSE(vortex::ContentRangeParser::parse("bytes 18446744073709551616-18446744073709551616/18446744073709551617").has_value());
}

TEST_CASE("Content-Range validation requires exact requested range and total") {
    const auto parsed = vortex::ContentRangeParser::parse("bytes 10-19/100");
    REQUIRE(parsed.has_value());
    CHECK(parsed->matches({10, 19}, 100));
    CHECK_FALSE(parsed->matches({10, 18}, 100));
    CHECK_FALSE(parsed->matches({10, 19}, 101));
}
