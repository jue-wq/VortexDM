#pragma once

#include "vortex/error.hpp"

#include <cstddef>
#include <cstdint>
#include <atomic>
#include <functional>
#include <filesystem>
#include <string>

namespace vortex {

struct RemoteFileInfo {
    std::string effective_url;
    std::string filename;
    std::string etag;
    std::string last_modified;
    std::uint64_t total_bytes{0};
    bool size_known{false};
    bool supports_ranges{false};
    long response_code{0};
};

struct DownloadOptions {
    std::size_t max_connections{8};
    std::uint64_t minimum_segment_bytes{2U * 1024U * 1024U};
    long connect_timeout_ms{10000};
    long transfer_timeout_ms{0};
    std::size_t max_retries{3};
    std::uint64_t retry_backoff_ms{500};
    const std::atomic_bool* cancel_requested{nullptr};
    std::function<void(std::uint64_t, std::uint64_t)> progress_callback;
};

[[nodiscard]] Result<RemoteFileInfo> probe_url(const std::string& url);
[[nodiscard]] Result<void> download_file(
    const std::string& url,
    const std::filesystem::path& destination,
    const DownloadOptions& options = {});

} // namespace vortex
