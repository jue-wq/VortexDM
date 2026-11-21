#pragma once

#include "vortex/crash_safe_file_writer.hpp"
#include "vortex/error.hpp"
#include "vortex/sqlite_repository.hpp"

namespace vortex {

// Enforces write-ahead ordering: durable file bytes first, journal checkpoint second.
[[nodiscard]] Result<void> commit_segment_checkpoint(
    CrashSafeFileWriter& writer,
    SqliteRepository& repository,
    const SegmentRecord& checkpoint);

} // namespace vortex
