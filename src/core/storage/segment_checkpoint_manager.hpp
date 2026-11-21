#pragma once

#include "core/common/result.hpp"
#include "core/storage/download_record.hpp"
#include "core/storage/offset_file_writer.hpp"
#include "core/storage/sqlite_database.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <span>
#include <string>
#include <vector>

namespace vortex {

// Orders segment persistence: write -> durable flush -> on-disk hash -> SQLite checkpoint.
// A failed checkpoint may leave bytes on disk, but those bytes remain untrusted and can be fetched again.
class SegmentCheckpointManager final {
public:
    SegmentCheckpointManager(
        OffsetFileWriter& writer, SqliteDatabase& database, std::filesystem::path partial_path
    );

    [[nodiscard]] Status commit_segment(
        std::int64_t download_id, std::int32_t segment_index,
        std::uint64_t offset, std::span<const std::byte> data,
        const std::string& expected_sha256, std::int64_t updated_at_unix
    );

    // Removes only checkpoints whose stored digest is malformed or no longer matches disk.
    // File I/O/hash failures are returned without deleting metadata.
    [[nodiscard]] Result<std::size_t> verify_and_prune(std::int64_t download_id);\n    [[nodiscard]] Result<std::vector<SegmentCheckpoint>> list_checkpoints(std::int64_t download_id);

private:
    OffsetFileWriter& writer_;
    SqliteDatabase& database_;
    std::filesystem::path partial_path_;
    std::mutex mutex_;
};

} // namespace vortex
