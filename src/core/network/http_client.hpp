#pragma once

#include "core/common/result.hpp"
#include "core/download/segment_planner.hpp"

#include <chrono>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace vortex {

struct HttpRequest final {
    std::string url;
    std::vector<std::string> headers;
    std::optional<ByteRange> range;
    std::chrono::milliseconds connect_timeout{10'000};
    std::chrono::milliseconds total_timeout{0}; // Zero means no total deadline.
    long maximum_redirects{5};
};

struct HttpResponse final {
    long status_code{};
    std::map<std::string, std::string, std::less<>> headers;
    std::uint64_t bytes_received{};
    std::string effective_url;
};

using HttpBodySink = std::function<bool(std::span<const std::byte>)>;

class HttpClient final {
public:
    // Streams response bytes to sink. Redirects are intentionally not followed:
    // callers must validate Location with HttpRequestPolicy before retrying.
    [[nodiscard]] static Result<HttpResponse> perform(
        const HttpRequest& request, const HttpBodySink& sink,
        std::stop_token stop_token = {}
    );
};

} // namespace vortex
