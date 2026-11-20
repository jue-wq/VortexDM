#pragma once

#include <cstdint>
#include <string>

namespace vortex {

enum class DownloadState : std::int32_t {
    queued = 0,
    running = 1,
    paused = 2,
    completed = 3,
    failed = 4,
    cancelled = 5
};

struct DownloadRecord {
    std::int64_t id{};
    std::string url;
    std::string destination;
    std::int64_t total_bytes{-1};
    std::int64_t downloaded_bytes{};
    DownloadState state{DownloadState::queued};
    std::string checksum_sha256;
    std::int64_t created_at_unix{};
    std::int64_t updated_at_unix{};
};

} // namespace vortex
