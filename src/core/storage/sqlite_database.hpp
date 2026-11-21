#pragma once

#include "core/common/result.hpp"
#include "download_record.hpp"

#include <filesystem>
#include <mutex>
#include <vector>

struct sqlite3;

namespace vortex {

class SqliteDatabase final {
public:
    SqliteDatabase() = default;
    ~SqliteDatabase();

    SqliteDatabase(const SqliteDatabase&) = delete;
    SqliteDatabase& operator=(const SqliteDatabase&) = delete;

    [[nodiscard]] Status open(const std::filesystem::path& path);
    [[nodiscard]] Status close();
    [[nodiscard]] Status migrate();
    // Converts tasks left running by an unclean shutdown to paused.
    // File-size/hash reconciliation must happen before a caller resumes them.
    [[nodiscard]] Status recover_interrupted_downloads(std::int64_t updated_at_unix);
    [[nodiscard]] Result<std::int64_t> insert_download(const DownloadRecord& record);
    [[nodiscard]] Status retry_failed_download(std::int64_t id, std::int64_t updated_at_unix);
    [[nodiscard]] Status update_download_progress(
        std::int64_t id, std::int64_t downloaded_bytes, DownloadState state,
        std::int64_t updated_at_unix
    );
    [[nodiscard]] Result<std::vector<DownloadRecord>> list_downloads();
    [[nodiscard]] Status delete_download(std::int64_t id);
    [[nodiscard]] Status record_segment_checkpoint(const SegmentCheckpoint& checkpoint);
    [[nodiscard]] Result<std::vector<SegmentCheckpoint>> list_segment_checkpoints(std::int64_t download_id);
    [[nodiscard]] Status clear_segment_checkpoints(std::int64_t download_id);
    // Removes one checkpoint so recovery can re-fetch only a corrupt segment.
    [[nodiscard]] Status erase_segment_checkpoint(std::int64_t download_id, std::int32_t segment_index);

private:
    [[nodiscard]] Status execute(const char* sql);
    [[nodiscard]] VortexError database_error(const char* context) const;

    sqlite3* db_{};
    mutable std::mutex mutex_;
};

} // namespace vortex
