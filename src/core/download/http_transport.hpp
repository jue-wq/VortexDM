#pragma once

#include "core/common/result.hpp"
#include "core/download/http_response_headers.hpp"

#include <cstddef>
#include <functional>
#include <span>
#include <string>
#include <string_view>

namespace vortex {

struct HttpTransferOptions final {
    long connect_timeout_seconds{15};
    long total_timeout_seconds{0}; // zero disables the overall deadline
    long maximum_redirects{5};
    std::string user_agent{"VortexDM/0.1"};
};

struct HttpTransferResponse final {
    long status_code{};
    HttpResponseHeaders headers;
    std::size_t body_bytes{};
};

using HttpBodySink = std::function<bool(std::span<const std::byte>)>;

// A bounded, TLS-verifying HTTP(S) streaming transport. It does not manage
// files, retries, cookies, or download state; callers own those policies.
class HttpTransport final {
public:
    [[nodiscard]] Result<HttpTransferResponse> get(
        std::string_view url,
        const HttpBodySink& sink,
        const HttpTransferOptions& options = {}
    ) const;
};

} // namespace vortex
