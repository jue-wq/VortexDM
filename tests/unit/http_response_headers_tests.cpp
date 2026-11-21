#include "core/download/http_response_headers.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string_view>

TEST_CASE("HTTP response headers parse range and retry metadata") {
    using vortex::HttpResponseHeaders;
    const std::array<std::string_view, 8> lines{
        "HTTP/1.1 206 Partial Content",
        "cOnTeNt-LeNgTh: 100",
        "Content-Range: bytes 50-149/500",
        "Accept-Ranges: none, bytes",
        "Retry-After: 12",
        "ETag: \"v1\"",
        "Last-Modified: Tue, 15 Nov 1994 08:12:31 GMT",
        ""
    };

    const auto parsed = HttpResponseHeaders::parse(lines);
    REQUIRE(parsed.has_value());
    CHECK(parsed->status_code == 206);
    REQUIRE(parsed->content_length.has_value());
    CHECK(*parsed->content_length == 100);
    REQUIRE(parsed->content_range.has_value());
    CHECK(*parsed->content_range == "bytes 50-149/500");
    CHECK(parsed->accepts_byte_ranges);
    REQUIRE(parsed->retry_after.has_value());
    CHECK(parsed->retry_after->count() == 12);
    CHECK(parsed->etag == "\"v1\"");
    CHECK(parsed->last_modified.has_value());
}

TEST_CASE("HTTP response headers reject conflicting or malformed metadata") {
    using vortex::HttpResponseHeaders;
    const std::array<std::string_view, 3> conflicting_length{
        "HTTP/1.1 200 OK", "Content-Length: 10", "content-length: 11"
    };
    CHECK_FALSE(HttpResponseHeaders::parse(conflicting_length).has_value());

    const std::array<std::string_view, 2> malformed_length{
        "HTTP/1.1 200 OK", "Content-Length: 10bytes"
    };
    CHECK_FALSE(HttpResponseHeaders::parse(malformed_length).has_value());

    const std::array<std::string_view, 2> duplicate_status{
        "HTTP/1.1 100 Continue", "HTTP/1.1 200 OK"
    };
    CHECK_FALSE(HttpResponseHeaders::parse(duplicate_status).has_value());

    const std::array<std::string_view, 2> no_status{
        "Content-Length: 1", "ETag: x"
    };
    CHECK_FALSE(HttpResponseHeaders::parse(no_status).has_value());
}

TEST_CASE("HTTP response headers ignore unsupported Retry-After date form") {
    const std::array<std::string_view, 2> lines{
        "HTTP/1.1 503 Service Unavailable",
        "Retry-After: Wed, 21 Oct 2015 07:28:00 GMT"
    };
    const auto parsed = vortex::HttpResponseHeaders::parse(lines);
    REQUIRE(parsed.has_value());
    CHECK_FALSE(parsed->retry_after.has_value());
}


TEST_CASE("Only valid strong entity tags are accepted for safe range resumption") {
    CHECK(vortex::is_strong_etag("\"stable-v1\""));
    CHECK(vortex::is_strong_etag("\"\""));
    CHECK_FALSE(vortex::is_strong_etag("W/\"weak-v1\""));
    CHECK_FALSE(vortex::is_strong_etag("stable-v1"));
    CHECK_FALSE(vortex::is_strong_etag("\"unterminated"));
    CHECK_FALSE(vortex::is_strong_etag("\"bad\\rvalue\""));
    CHECK_FALSE(vortex::is_strong_etag("\"bad\\nvalue\""));
    CHECK_FALSE(vortex::is_strong_etag(""));
}
