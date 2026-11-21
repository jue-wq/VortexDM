#include "core/download/range_download_coordinator.hpp"

#include "core/download/content_range.hpp"
#include "core/download/retry_policy.hpp"

#include <algorithm>
#include <charconv>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

namespace vortex {
namespace {

bool parse_content_length(const std::string& value, std::uint64_t& length) noexcept {
    if (value.empty()) return false;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), length);
    return parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size();
}

std::optional<std::chrono::seconds> parse_retry_after(
    const std::map<std::string, std::string, std::less<>>& headers
) noexcept {
    const auto found = headers.find("retry-after");
    if (found == headers.end() || found->second.empty()) return std::nullopt;
    std::uint64_t seconds = 0;
    const auto parsed = std::from_chars(
        found->second.data(), found->second.data() + found->second.size(), seconds);
    if (parsed.ec != std::errc{} ||
        parsed.ptr != found->second.data() + found->second.size() ||
        seconds > static_cast<std::uint64_t>(
            std::numeric_limits<std::chrono::seconds::rep>::max())) {
        return std::nullopt;
    }
    return std::chrono::seconds{static_cast<std::chrono::seconds::rep>(seconds)};
}

bool default_wait(std::chrono::milliseconds delay, std::stop_token stop_token) {
    std::mutex mutex;
    std::condition_variable_any condition;
    std::unique_lock lock(mutex);
    (void)condition.wait_for(lock, stop_token, delay, [] { return false; });
    return !stop_token.stop_requested();
}

} // namespace

Status RangeDownloadCoordinator::transfer(
    const std::string_view url, const std::uint64_t total_size,
    const std::uint64_t chunk_size, OffsetFileWriter& writer,
    const RangeRequestExecutor& executor, const RangeTransferOptions& options,
    const std::stop_token stop_token
) {
    if (url.empty() || total_size == 0 ||
        total_size > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) ||
        chunk_size == 0 || chunk_size > maximum_staged_bytes || !executor ||
        options.base_retry_delay.count() < 0 ||
        options.maximum_retry_delay.count() < 0) {
        return std::unexpected(VortexError::make(
            ErrorCode::invalid_argument, "Invalid ranged download parameters"));
    }

    const auto& waiter = options.wait_before_retry;
    std::uint64_t offset = 0;
    while (offset < total_size) {
        if (stop_token.stop_requested()) {
            return std::unexpected(VortexError::make(
                ErrorCode::cancelled, "Ranged download cancelled"));
        }

        const auto length = std::min(chunk_size, total_size - offset);
        const ByteRange range{offset, offset + length - 1U};
        HttpRequest request;
        request.url = std::string(url);
        request.range = range;

        std::vector<std::byte> staged;
        staged.reserve(static_cast<std::size_t>(length));
        HttpResponse accepted_response;
        bool accepted = false;

        for (std::uint32_t attempt = 0;; ++attempt) {
            staged.clear();
            const auto response = executor(request,
                [&staged, length](const std::span<const std::byte> bytes) {
                    if (static_cast<std::uint64_t>(bytes.size()) >
                        length - static_cast<std::uint64_t>(staged.size())) {
                        return false;
                    }
                    staged.insert(staged.end(), bytes.begin(), bytes.end());
                    return true;
                }, stop_token);

            RetryDecision decision{};
            if (!response) {
                if (response.error().code != ErrorCode::network_error) {
                    return std::unexpected(response.error());
                }
                decision = RetryPolicy::decide(
                    attempt, options.maximum_retries, 0, std::nullopt,
                    options.base_retry_delay, options.maximum_retry_delay);
                if (!decision.retry) return std::unexpected(response.error());
            } else {
                if (response->status_code < 100 || response->status_code > 599) {
                    return std::unexpected(VortexError::make(
                        ErrorCode::network_error, "HTTP server returned an invalid status"));
                }
                const auto status = static_cast<int>(response->status_code);
                if (status == 408 || status == 429 || status >= 500) {
                    decision = RetryPolicy::decide(
                        attempt, options.maximum_retries, status,
                        parse_retry_after(response->headers),
                        options.base_retry_delay, options.maximum_retry_delay);
                    if (!decision.retry) {
                        return std::unexpected(VortexError::make(
                            ErrorCode::network_error, "HTTP range request failed after retries"));
                    }
                } else {
                    accepted_response = *response;
                    accepted = true;
                    break;
                }
            }

            const auto wait = waiter ? waiter : RetryWaiter{default_wait};
            if (stop_token.stop_requested() || !wait(decision.delay, stop_token)) {
                return std::unexpected(VortexError::make(
                    ErrorCode::cancelled, "Ranged download cancelled during retry backoff"));
            }
        }

        const auto range_header = accepted_response.headers.find("content-range");
        const auto length_header = accepted_response.headers.find("content-length");
        std::uint64_t declared_length = 0;
        if (!accepted || range_header == accepted_response.headers.end() ||
            length_header == accepted_response.headers.end() ||
            !parse_content_length(length_header->second, declared_length) ||
            !ContentRangeParser::validate_partial_response(
                static_cast<int>(accepted_response.status_code), range_header->second,
                declared_length, range, total_size) ||
            accepted_response.bytes_received != length ||
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
