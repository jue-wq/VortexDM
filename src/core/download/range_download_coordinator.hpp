#pragma once

#include "core/common/result.hpp"
#include "core/network/http_client.hpp"
#include "core/storage/offset_file_writer.hpp"
#include "core/storage/segment_checkpoint_manager.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <stop_token>
#include <string_view>

namespace vortex {

using RangeRequestExecutor = std::function<Result<HttpResponse>(
    const HttpRequest&, const HttpBodySink&, std::stop_token)>;
using RetryWaiter = std::function<bool(std::chrono::milliseconds, std::stop_token)>;

struct RangeTransferOptions final {
    std::uint32_t maximum_retries{3};
    // Defaults to one for compatibility; values above one enable bounded parallel requests.
    std::uint32_t maximum_concurrent_requests{1};
    std::chrono::milliseconds base_retry_delay{500};
    std::chrono::milliseconds maximum_retry_delay{30'000};
    // When concurrency is enabled, the waiter must be safe for concurrent calls.
    // Return false to abort (normally because cancellation was requested).
    RetryWaiter wait_before_retry;
};

class RangeDownloadCoordinator final {
public:
    static constexpr std::uint64_t maximum_staged_bytes = 4U * 1024U * 1024U;

    // Downloads bounded ranges into an already-open, pre-sized writer.
    // Each response is validated in a per-worker staging buffer before commit.
    // If concurrency is enabled, executor calls may overlap and the executor must
    // support concurrent invocation. The built-in HttpClient executor does.
    [[nodiscard]] static Status transfer(
        std::string_view url, std::uint64_t total_size,
        std::uint64_t chunk_size, OffsetFileWriter& writer,
        const RangeRequestExecutor& executor = &HttpClient::perform,
        const RangeTransferOptions& options = {},
        std::stop_token stop_token = {},
        SegmentCheckpointManager* checkpoint_manager = nullptr,
        std::int64_t download_id = 0, std::int64_t updated_at_unix = 0
    );
};

} // namespace vortex
