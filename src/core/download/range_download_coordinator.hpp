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
    std::chrono::milliseconds base_retry_delay{500};
    std::chrono::milliseconds maximum_retry_delay{30'000};
    // Return false to abort (normally because cancellation was requested).
    RetryWaiter wait_before_retry;
};

class RangeDownloadCoordinator final {
public:
    static constexpr std::uint64_t maximum_staged_bytes = 4U * 1024U * 1024U;

    // Downloads sequential bounded ranges into an already-open, pre-sized writer.
    // Each response is fully validated in memory before any bytes from that range
    // are committed to the destination file. The executor may be injected in tests.
    [[nodiscard]] static Status transfer(
        std::string_view url, std::uint64_t total_size,
        std::uint64_t chunk_size, OffsetFileWriter& writer,
        const RangeRequestExecutor& executor = &HttpClient::perform,
        const RangeTransferOptions& options = {},
        std::stop_token stop_token = {}
    );
};

} // namespace vortex
