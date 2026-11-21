#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace vortex {

// Only a syntactically valid, non-weak entity-tag is safe for If-Range.
[[nodiscard]] bool is_strong_etag(std::string_view value) noexcept;

struct HttpResponseHeaders final {
    int status_code{};
    std::optional<std::uint64_t> content_length;
    std::optional<std::string> content_range;
    bool accepts_byte_ranges{};
    std::optional<std::chrono::seconds> retry_after;
    std::optional<std::string> etag;
    std::optional<std::string> last_modified;

    // Parses one complete HTTP response header block. Header names are
    // case-insensitive; conflicting duplicate metadata is rejected.
    // Retry-After delta-seconds is supported; HTTP-date form is ignored.
    [[nodiscard]] static std::optional<HttpResponseHeaders> parse(
        std::span<const std::string_view> lines
    );
};

} // namespace vortex
