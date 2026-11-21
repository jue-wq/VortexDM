#include "core/download/range_download_coordinator.hpp"

#include "core/download/content_range.hpp"
#include "core/download/retry_policy.hpp"
#include "core/common/crypto_utils.hpp"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <stop_token>
#include <string>
#include <system_error>
#include <thread>
#include <unordered_set>
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
    const std::stop_token stop_token, SegmentCheckpointManager* checkpoint_manager,
    const std::int64_t download_id, const std::int64_t updated_at_unix
) {
    if (url.empty() || total_size == 0 ||
        total_size > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) ||
        chunk_size == 0 || chunk_size > maximum_staged_bytes || !executor ||
        options.base_retry_delay.count() < 0 ||
        options.maximum_retry_delay.count() < 0 ||
        options.maximum_concurrent_requests == 0 ||
        options.maximum_concurrent_requests > 16 ||
        (checkpoint_manager != nullptr && download_id <= 0)) {
        return std::unexpected(VortexError::make(
            ErrorCode::invalid_argument, "Invalid ranged download parameters"));
    }

    const auto segment_count = (total_size - 1U) / chunk_size + 1U;
    if (segment_count > static_cast<std::uint64_t>(
            std::numeric_limits<std::int32_t>::max()) + 1U) {
        return std::unexpected(VortexError::make(
            ErrorCode::invalid_argument, "Download requires too many range segments"));
    }

    std::unordered_set<std::int32_t> completed_segments;
    if (checkpoint_manager != nullptr) {
        const auto pruned = checkpoint_manager->verify_and_prune(download_id);
        if (!pruned) return std::unexpected(pruned.error());
        const auto listed = checkpoint_manager->list_checkpoints(download_id);
        if (!listed) return std::unexpected(listed.error());
        completed_segments.reserve(listed->size());
        for (const auto& checkpoint : *listed) {
            if (checkpoint.segment_index < 0) {
                return std::unexpected(VortexError::make(
                    ErrorCode::invalid_argument, "Stored checkpoint has a negative segment index"));
            }
            const auto index = static_cast<std::uint64_t>(checkpoint.segment_index);
            if (index >= segment_count) {
                return std::unexpected(VortexError::make(
                    ErrorCode::invalid_argument, "Stored checkpoint is outside the current download geometry"));
            }
            const auto expected_offset = index * chunk_size;
            const auto expected_length = std::min(chunk_size, total_size - expected_offset);
            if (checkpoint.offset != static_cast<std::int64_t>(expected_offset) ||
                checkpoint.length != static_cast<std::int64_t>(expected_length)) {
                return std::unexpected(VortexError::make(
                    ErrorCode::invalid_argument, "Stored checkpoint does not match the current chunk geometry"));
            }
            completed_segments.insert(checkpoint.segment_index);
        }
    }

    std::stop_source workers_stop;
    std::stop_callback external_cancel(stop_token, [&workers_stop] {
        workers_stop.request_stop();
    });
    const auto worker_token = workers_stop.get_token();
    std::atomic<std::uint64_t> next_segment{0};
    std::mutex error_mutex;
    std::optional<VortexError> first_error;

    const auto fail = [&](VortexError error) {
        {
            std::scoped_lock lock(error_mutex);
            if (!first_error) first_error = std::move(error);
        }
        workers_stop.request_stop();
    };

    const auto process_segment = [&](const std::uint64_t index) -> Status {
        if (worker_token.stop_requested()) {
            return std::unexpected(VortexError::make(
                ErrorCode::cancelled, "Ranged download cancelled"));
        }

        const auto offset = index * chunk_size;
        const auto length = std::min(chunk_size, total_size - offset);
        const auto segment_index = static_cast<std::int32_t>(index);
        if (completed_segments.contains(segment_index)) return {};

        const ByteRange range{offset, offset + length - 1U};
        HttpRequest request;
        request.url = std::string(url);
        request.range = range;

        std::vector<std::byte> staged;
        staged.reserve(static_cast<std::size_t>(length));
        HttpResponse accepted_response;
        bool accepted = false;

        for (std::uint32_t attempt = 0;; ++attempt) {
            if (worker_token.stop_requested()) {
                return std::unexpected(VortexError::make(
                    ErrorCode::cancelled, "Ranged download cancelled"));
            }
            staged.clear();
            const auto response = executor(request,
                [&staged, length](const std::span<const std::byte> bytes) {
                    if (static_cast<std::uint64_t>(bytes.size()) >
                        length - static_cast<std::uint64_t>(staged.size())) {
                        return false;
                    }
                    staged.insert(staged.end(), bytes.begin(), bytes.end());
                    return true;
                }, worker_token);

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

            const auto wait = options.wait_before_retry
                ? options.wait_before_retry : RetryWaiter{default_wait};
            if (worker_token.stop_requested() || !wait(decision.delay, worker_token)) {
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

        if (checkpoint_manager != nullptr) {
            const auto digest = CryptoUtils::sha256_bytes(staged);
            if (!digest) return std::unexpected(digest.error());
            const auto committed = checkpoint_manager->commit_segment(
                download_id, segment_index, offset, staged, *digest, updated_at_unix);
            if (!committed) return committed;
        } else {
            const auto written = writer.write_at(offset, staged);
            if (!written) return written;
        }
        return {};
    };

    const auto worker_count = static_cast<std::size_t>(
        std::min<std::uint64_t>(options.maximum_concurrent_requests, segment_count));
    const auto run_worker = [&] {
        while (!worker_token.stop_requested()) {
            const auto index = next_segment.fetch_add(1, std::memory_order_relaxed);
            if (index >= segment_count) return;
            const auto status = process_segment(index);
            if (!status) {
                if (status.error().code != ErrorCode::cancelled ||
                    !worker_token.stop_requested()) {
                    fail(status.error());
                }
                return;
            }
        }
    };

    if (worker_count == 1U) {
        // Preserve caller-thread executor semantics for the default sequential mode.
        run_worker();
    } else {
        std::vector<std::jthread> workers;
        workers.reserve(worker_count);
        for (std::size_t worker = 0; worker < worker_count; ++worker) {
            workers.emplace_back(run_worker);
        }
        for (auto& worker : workers) {
            if (worker.joinable()) worker.join();
        }
    }

    {
        std::scoped_lock lock(error_mutex);
        if (first_error) return std::unexpected(*first_error);
    }
    if (stop_token.stop_requested()) {
        return std::unexpected(VortexError::make(
            ErrorCode::cancelled, "Ranged download cancelled"));
    }
    return writer.flush();
}

} // namespace vortex
