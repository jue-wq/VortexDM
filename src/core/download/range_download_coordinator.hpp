#pragma once

#include "core/common/result.hpp"
#include "core/network/http_client.hpp"
#include "core/storage/offset_file_writer.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <stop_token>
#include <string_view>

namespace vortex {

using RangeRequestExecutor = std::function<Result<HttpResponse>(
    const HttpRequest&, const HttpBodySink&, std::stop_token)>;

class RangeDownloadCoordinator final {
public:
    static constexpr std::uint64_t maximum_staged_bytes = 4U * 1024U * 1024U;

    // Downloads sequential bounded ranges into an already-open, pre-sized writer.
    // Each response is fully validated in memory before any bytes from that range
    // are committed to the destination file.
    [[nodiscard]] static Status transfer(
        std::string_view url, std::uint64_t total_size,
        std::uint64_t chunk_size, OffsetFileWriter& writer,
        const RangeRequestExecutor& executor = &HttpClient::perform,
        std::stop_token stop_token = {}
    );
};

} // namespace vortex
