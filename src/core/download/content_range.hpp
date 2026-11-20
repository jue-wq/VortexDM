#pragma once

#include "core/download/segment_planner.hpp"

#include <cstdint>
#include <optional>
#include <string_view>

namespace vortex {

struct ContentRange final {
    ByteRange range;
    std::uint64_t total_size{};

    [[nodiscard]] bool matches(const ByteRange& requested,
                               std::uint64_t expected_total) const noexcept {
        return range.first == requested.first && range.last == requested.last &&
               total_size == expected_total;
    }
};

class ContentRangeParser final {
public:
    // Parses a satisfied byte range: "bytes first-last/total".
    // Unsatisfied ranges and unknown totals are intentionally rejected.
    [[nodiscard]] static std::optional<ContentRange> parse(
        std::string_view header
    ) noexcept;

    // A segment is writable only for an exact 206 response with a matching
    // Content-Range and Content-Length. This rejects truncated/overlong bodies.
    [[nodiscard]] static bool validate_partial_response(
        int http_status, std::string_view content_range,
        std::uint64_t content_length, const ByteRange& requested,
        std::uint64_t expected_total
    ) noexcept;
};

} // namespace vortex
