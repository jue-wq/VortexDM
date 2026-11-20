#pragma once

#include "core/download/segment_planner.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace vortex {

// Validates the URL boundary and constructs safe byte-range request metadata.
// The eventual HTTP transport must still enforce TLS and redirect policy.
class HttpRequestPolicy final {
public:
    [[nodiscard]] static bool is_supported_url(std::string_view url) noexcept;
    [[nodiscard]] static bool is_allowed_redirect(
        std::string_view original_url, std::string_view redirect_url
    ) noexcept;
    [[nodiscard]] static std::optional<std::string> range_header(
        const ByteRange& range
    );
};

} // namespace vortex
