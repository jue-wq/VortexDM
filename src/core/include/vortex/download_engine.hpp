#pragma once

#include "vortex/error.hpp"
#include "vortex/crash_safe_file_writer.hpp"
#include "vortex/segment_scheduler.hpp"

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
    std::function<Result<void>(const RemoteFileInfo&)> stream_info_callback;
    std::function<Result<void>(CrashSafeFileWriter&, const ByteRange&, std::uint64_t)> segment_checkpoint_callback;
    std::function<Result<void>()> reset_segment_checkpoints_callback;
};

[[nodiscard]] Result<RemoteFileInfo> probe_url(const std::string& url);
[[nodiscard]] Result<std::uint64_t> download_range_to_writer(
    const std::string& url, const ByteRange& range, CrashSafeFileWriter& writer,
    const DownloadOptions& options = {});
[[nodiscard]] Result<void> download_file(
    const std::string& url,
    const std::filesystem::path& destination,
    const DownloadOptions& options = {});

} // namespace vortex
