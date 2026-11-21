#pragma once

#include "vortex/crash_safe_file_writer.hpp"
#include "vortex/error.hpp"
#include "vortex/sqlite_repository.hpp"
#include "vortex/segment_scheduler.hpp"

#include <filesystem>
#include <functional>

namespace vortex {

using RecoveryRangeFetcher = std::function<Result<std::uint64_t>(CrashSafeFileWriter&, const ByteRange&)>;

// Reopens an existing partial file, reconstructs missing intervals from SQLite,
// downloads each missing interval through the supplied range fetcher, then
// fsyncs bytes before writing each updated segment checkpoint.
[[nodiscard]] Result<void> recover_download_segments(
    const std::filesystem::path& partial_path,
    std::uint64_t total_bytes,
    const std::string& stream_id,
    SqliteRepository& repository,
    const RecoveryRangeFetcher& fetch_range);

} // namespace vortex
