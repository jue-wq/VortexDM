#include "core/network/http_request_policy.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>

TEST_CASE("HTTP request policy accepts only HTTP and HTTPS URLs") {
    using vortex::HttpRequestPolicy;
    CHECK(HttpRequestPolicy::is_supported_url("https://example.com/file.bin"));
    CHECK(HttpRequestPolicy::is_supported_url("HTTP://example.com/path?q=1"));
    CHECK_FALSE(HttpRequestPolicy::is_supported_url(""));
    CHECK_FALSE(HttpRequestPolicy::is_supported_url("file:///etc/passwd"));
    CHECK_FALSE(HttpRequestPolicy::is_supported_url("javascript:alert(1)"));
    CHECK_FALSE(HttpRequestPolicy::is_supported_url("ftp://example.com/file"));
    CHECK_FALSE(HttpRequestPolicy::is_supported_url("https:///missing-host"));
    CHECK_FALSE(HttpRequestPolicy::is_supported_url("https://user:pass@example.com/"));
    CHECK_FALSE(HttpRequestPolicy::is_supported_url("https://example.com/a b"));
    CHECK_FALSE(HttpRequestPolicy::is_supported_url("https://example.com\\\\path"));
}

TEST_CASE("HTTP redirect policy prevents HTTPS downgrade") {
    using vortex::HttpRequestPolicy;
    CHECK(HttpRequestPolicy::is_allowed_redirect(
        "https://example.com/a", "https://cdn.example.com/a"));
    CHECK_FALSE(HttpRequestPolicy::is_allowed_redirect(
        "https://example.com/a", "http://cdn.example.com/a"));
    CHECK(HttpRequestPolicy::is_allowed_redirect(
        "http://example.com/a", "https://cdn.example.com/a"));
    CHECK_FALSE(HttpRequestPolicy::is_allowed_redirect(
        "http://example.com/a", "file:///tmp/file"));
}

TEST_CASE("HTTP range request header is inclusive and overflow-safe") {
    using vortex::HttpRequestPolicy;
    const auto header = HttpRequestPolicy::range_header({128, 255});
    REQUIRE(header.has_value());
    CHECK(*header == "bytes=128-255");
    CHECK_FALSE(HttpRequestPolicy::range_header({10, 9}).has_value());
    CHECK_FALSE(HttpRequestPolicy::range_header({
        0, std::numeric_limits<std::uint64_t>::max()
    }).has_value());
}
