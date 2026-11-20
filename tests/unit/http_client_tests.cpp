#include "core/network/http_client.hpp"

#include <catch2/catch_test_macros.hpp>

#include <stop_token>

TEST_CASE("HTTP client rejects unsupported schemes before opening a socket") {
    const auto response = vortex::HttpClient::perform(
        vortex::HttpRequest{.url = "file:///etc/passwd"},
        [](std::span<const std::byte>) { return true; }
    );
    REQUIRE_FALSE(response.has_value());
    CHECK(response.error().code == vortex::ErrorCode::invalid_argument);
}

TEST_CASE("HTTP client observes cancellation before starting a transfer") {
    std::stop_source source;
    source.request_stop();
    const auto response = vortex::HttpClient::perform(
        vortex::HttpRequest{.url = "https://example.com/"},
        [](std::span<const std::byte>) { return true; },
        source.get_token()
    );
    REQUIRE_FALSE(response.has_value());
    CHECK(response.error().code == vortex::ErrorCode::cancelled);
}

TEST_CASE("HTTP client rejects malformed caller-supplied headers") {
    const auto response = vortex::HttpClient::perform(
        vortex::HttpRequest{
            .url = "https://example.com/",
            .headers = {"X-Test: safe\r\nInjected: true"}
        },
        [](std::span<const std::byte>) { return true; }
    );
    REQUIRE_FALSE(response.has_value());
    CHECK(response.error().code == vortex::ErrorCode::invalid_argument);
}

TEST_CASE("HTTP client rejects invalid header field names and control bytes") {
    const auto rejects = [](std::string header) {
        const auto response = vortex::HttpClient::perform(
            vortex::HttpRequest{
                .url = "https://example.com/",
                .headers = {std::move(header)}
            },
            [](std::span<const std::byte>) { return true; }
        );
        REQUIRE_FALSE(response.has_value());
        CHECK(response.error().code == vortex::ErrorCode::invalid_argument);
    };

    rejects(": missing-name");
    rejects("Bad Header: value");
    rejects(std::string{"X-Test: value"} + '\0' + "hidden");
    rejects(std::string{"X-Test: value"} + static_cast<char>(1) + "hidden");
    rejects("X-Test: value\\r\\nInjected: true");
}
