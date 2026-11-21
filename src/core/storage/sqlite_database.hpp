#pragma once

#include "core/common/result.hpp"
#include "download_record.hpp"

#include <filesystem>
#include <mutex>
#include <string_view>
#include <vector>

struct sqlite3;

namespace vortex {

struct NativeInboxEntry {
    std::string request_id;
    DownloadRecord download;
};

class SqliteDatabase final {
public:
    SqliteDatabase() = default;
    ~SqliteDatabase();

    SqliteDatabase(const SqliteDatabase&) = delete;
    SqliteDatabase& operator=(const SqliteDatabase&) = delete;

    [[nodiscard]] Status open(const std::filesystem::path& path);
    [[nodiscard]] Status close();
    [[nodiscard]] Status migrate();
    [[nodiscard]] Result<std::int64_t> insert_download(const DownloadRecord& record);
    // Atomically inserts a download and binds a stable IPC request ID. Replays return the original download ID.
    [[nodiscard]] Result<std::int64_t> enqueue_download_once(std::string_view request_id, const DownloadRecord& record);
    [[nodiscard]] Result<std::vector<NativeInboxEntry>> list_pending_native_requests(std::size_t limit = 100);
    [[nodiscard]] Status mark_native_request_imported(std::string_view request_id);
    [[nodiscard]] Status update_download_progress(
        std::int64_t id, std::int64_t downloaded_bytes, DownloadState state,
        std::int64_t updated_at_unix
    );
    [[nodiscard]] Result<std::vector<DownloadRecord>> list_downloads();
    [[nodiscard]] Status delete_download(std::int64_t id);

private:
    [[nodiscard]] Status execute(const char* sql);
    [[nodiscard]] VortexError database_error(const char* context) const;

    sqlite3* db_{};
    mutable std::mutex mutex_;
};

} // namespace vortex
