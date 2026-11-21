#include "core/download/http_transport.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("HTTP transport rejects invalid arguments before opening a connection") {
    vortex::HttpTransport transport;
    const vortex::HttpBodySink sink = [](std::span<const std::byte>) { return true; };

    CHECK_FALSE(transport.get("", sink).has_value());
    CHECK_FALSE(transport.get("https://example.com", {}).has_value());

    vortex::HttpTransferOptions invalid_timeout;
    invalid_timeout.connect_timeout_seconds = 0;
    CHECK_FALSE(transport.get("https://example.com", sink, invalid_timeout).has_value());

    invalid_timeout = {};
    invalid_timeout.maximum_redirects = -1;
    CHECK_FALSE(transport.get("https://example.com", sink, invalid_timeout).has_value());
}
