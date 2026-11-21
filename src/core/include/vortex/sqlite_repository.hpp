#pragma once

#include "vortex/error.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>
#include <sqlite3.h>

namespace vortex {

struct DownloadRecord {
    std::string id;
    std::string task_type{"http_single"};
    std::string url;
    std::string filename;
    std::string save_dir;
    std::string status{"Added"};
    std::uint64_t total_bytes{0};
    std::uint64_t downloaded_bytes{0};
    std::string etag;
    std::string last_modified;
    std::string headers_json{"{}"};
    std::string extractor_origin_url;
    std::string video_format_id;
    std::string audio_format_id;
};

struct StreamRecord {
    std::string id;
    std::string download_id;
    std::string stream_role{"primary"};
    std::string url;
    std::string temp_path;
    std::uint64_t total_bytes{0};
    std::uint64_t downloaded_bytes{0};
    std::string protocol{"http"};
    std::string format_id;
    std::string headers_json{"{}"};
    std::string status{"Added"};
};

struct SegmentRecord {
    std::string stream_id;
    std::uint64_t segment_index{0};
    std::uint64_t start_offset{0};
    std::uint64_t end_offset{0};
    std::uint64_t downloaded_bytes{0};
    std::string status{"Pending"};
};

struct YtDlpProfileRecord {
    std::string id;
    std::string name;
    std::string source_type;
    std::string repo_slug;
    std::string custom_api_url;
    std::string asset_regex;
    std::string pinned_tag;
    bool include_prereleases{false};
    std::string local_binary_path;
    std::string python_path;
    std::string installed_version;
    std::string last_etag;
    bool is_active{false};
};

class SqliteRepository {
public:
    explicit SqliteRepository(const std::filesystem::path& path);
    ~SqliteRepository();

    SqliteRepository(const SqliteRepository&) = delete;
    SqliteRepository& operator=(const SqliteRepository&) = delete;

    [[nodiscard]] Result<void> initialize();
    [[nodiscard]] Result<void> save_download(const DownloadRecord& record);
    [[nodiscard]] Result<void> save_stream(const StreamRecord& record);
    [[nodiscard]] Result<void> save_segment_checkpoint(const SegmentRecord& record);
    [[nodiscard]] Result<void> save_ytdlp_profile(const YtDlpProfileRecord& record);
    [[nodiscard]] Result<std::vector<YtDlpProfileRecord>> list_ytdlp_profiles();
    [[nodiscard]] Result<std::vector<SegmentRecord>> list_segments(const std::string& stream_id);
    [[nodiscard]] Result<std::vector<DownloadRecord>> list_downloads();
    [[nodiscard]] Result<std::uint64_t> schema_version();

private:
    [[nodiscard]] Result<void> execute(const char* sql);
    [[nodiscard]] Result<void> migrate();

    sqlite3* db_{nullptr};
    std::string open_error_;
};

} // namespace vortex
