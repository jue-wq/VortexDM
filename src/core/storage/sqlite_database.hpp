#pragma once

#include "core/common/result.hpp"
#include "download_record.hpp"
#include "core/download/http_response_headers.hpp"

#include <cstddef>
#include <filesystem>
#include <mutex>
#include <optional>
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
    // Atomically claims the oldest queued item and changes it to running.
    [[nodiscard]] Result<std::optional<DownloadRecord>> claim_next_queued_download(
        std::int64_t updated_at_unix
    );
    // Atomically inserts a download and binds a stable IPC request ID. Replays return the original download ID.
    [[nodiscard]] Result<std::int64_t> enqueue_download_once(std::string_view request_id, const DownloadRecord& record);
    [[nodiscard]] Result<std::vector<NativeInboxEntry>> list_pending_native_requests(std::size_t limit = 100);
    [[nodiscard]] Status mark_native_request_imported(std::string_view request_id);
    // Compare-and-swap state transition; rejects illegal edges and stale worker updates.
    [[nodiscard]] Status transition_download_state(
        std::int64_t id, DownloadState expected_from, DownloadState to,
        std::int64_t updated_at_unix
    );
    // Stores only a strong ETag; an empty value clears the resume validator.
    [[nodiscard]] Status update_download_validator(std::int64_t id, std::string_view etag);
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
