#include "core/download/range_download_coordinator.hpp"

#include "core/download/content_range.hpp"

#include <algorithm>
#include <charconv>
#include <limits>
#include <string>
#include <system_error>
#include <vector>

namespace vortex {
namespace {

bool parse_content_length(const std::string& value, std::uint64_t& length) noexcept {
    if (value.empty()) return false;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), length);
    return parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size();
}

} // namespace

Status RangeDownloadCoordinator::transfer(
    const std::string_view url, const std::uint64_t total_size,
    const std::uint64_t chunk_size, OffsetFileWriter& writer,
    const RangeRequestExecutor& executor, const std::stop_token stop_token
) {
    if (url.empty() || total_size == 0 ||
        total_size > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) ||
        chunk_size == 0 || chunk_size > maximum_staged_bytes || !executor) {
        return std::unexpected(VortexError::make(
            ErrorCode::invalid_argument, "Invalid ranged download parameters"));
    }

    std::uint64_t offset = 0;
    while (offset < total_size) {
        if (stop_token.stop_requested()) {
            return std::unexpected(VortexError::make(
                ErrorCode::cancelled, "Ranged download cancelled"));
        }

        const auto length = std::min(chunk_size, total_size - offset);
        const ByteRange range{offset, offset + length - 1U};
        std::vector<std::byte> staged;
        staged.reserve(static_cast<std::size_t>(length));

        HttpRequest request;
        request.url = std::string(url);
        request.range = range;

        const auto response = executor(request,
            [&staged, length](const std::span<const std::byte> bytes) {
                if (bytes.size() > length - static_cast<std::uint64_t>(staged.size())) {
                    return false;
                }
                staged.insert(staged.end(), bytes.begin(), bytes.end());
                return true;
            }, stop_token);
        if (!response) return std::unexpected(response.error());

        const auto range_header = response->headers.find("content-range");
        const auto length_header = response->headers.find("content-length");
        std::uint64_t declared_length = 0;
        if (range_header == response->headers.end() ||
            length_header == response->headers.end() ||
            !parse_content_length(length_header->second, declared_length) ||
            !ContentRangeParser::validate_partial_response(
                static_cast<int>(response->status_code), range_header->second,
                declared_length, range, total_size) ||
            response->bytes_received != length ||
            staged.size() != length) {
            return std::unexpected(VortexError::make(
                ErrorCode::network_error,
                "Server returned an invalid or incomplete byte-range response"));
        }

        const auto written = writer.write_at(offset, staged);
        if (!written) return written;
        offset += length;
    }

    return writer.flush();
}

} // namespace vortex
