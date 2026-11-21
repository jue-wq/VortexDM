#include "vortex/sqlite_repository.hpp"

#include <limits>
#include <algorithm>
#include <memory>
#include <string>
#include <utility>

namespace vortex {
namespace {

class Statement {
public:
    Statement(sqlite3* database, const char* sql) {
        if (database != nullptr) {
            result_ = sqlite3_prepare_v2(database, sql, -1, &statement_, nullptr);
        }
    }
    ~Statement() {
        if (statement_ != nullptr) sqlite3_finalize(statement_);
    }
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    [[nodiscard]] bool valid() const noexcept { return result_ == SQLITE_OK && statement_ != nullptr; }
    [[nodiscard]] sqlite3_stmt* get() const noexcept { return statement_; }
    [[nodiscard]] int result() const noexcept { return result_; }

private:
    sqlite3_stmt* statement_{nullptr};
    int result_{SQLITE_MISUSE};
};

[[nodiscard]] Result<sqlite3_int64> as_sqlite_integer(std::uint64_t value, const char* field) {
    if (value > static_cast<std::uint64_t>(std::numeric_limits<sqlite3_int64>::max())) {
        return std::unexpected(make_error("sqlite_integer_range", std::string(field) + " exceeds SQLite INTEGER range"));
    }
    return static_cast<sqlite3_int64>(value);
}

[[nodiscard]] Result<void> bind_text(sqlite3_stmt* statement, int index, const std::string& value) {
    const int rc = sqlite3_bind_text(statement, index, value.c_str(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
    if (rc != SQLITE_OK) {
        return std::unexpected(make_error("sqlite_bind", "Could not bind SQLite text parameter"));
    }
    return {};
}

[[nodiscard]] Error database_error(sqlite3* database, const std::string& code, const std::string& context) {
    return make_error(code, context + ": " + (database != nullptr ? sqlite3_errmsg(database) : "database is not open"));
}

} // namespace

SqliteRepository::SqliteRepository(const std::filesystem::path& path) {
    const int rc = sqlite3_open_v2(path.string().c_str(), &db_,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr);
    if (rc != SQLITE_OK) {
        open_error_ = db_ != nullptr ? sqlite3_errmsg(db_) : "SQLite could not allocate a connection";
    } else {
        sqlite3_busy_timeout(db_, 5000);
    }
}

SqliteRepository::~SqliteRepository() {
    if (db_ != nullptr) sqlite3_close(db_);
}

Result<void> SqliteRepository::execute(const char* sql) {
    if (db_ == nullptr) {
        return std::unexpected(make_error("sqlite_open", open_error_.empty() ? "Could not open database" : open_error_));
    }
    char* message = nullptr;
    const int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &message);
    if (rc != SQLITE_OK) {
        std::string detail = message != nullptr ? message : sqlite3_errmsg(db_);
        sqlite3_free(message);
        return std::unexpected(make_error("sqlite_exec", detail));
    }
    return {};
}

Result<void> SqliteRepository::initialize() {
    if (db_ == nullptr) {
        return std::unexpected(make_error("sqlite_open", open_error_.empty() ? "Could not open database" : open_error_));
    }
    auto wal = execute("PRAGMA journal_mode=WAL;");
    if (!wal) return wal;
    auto synchronous = execute("PRAGMA synchronous=NORMAL;");
    if (!synchronous) return synchronous;
    auto foreign_keys = execute("PRAGMA foreign_keys=ON;");
    if (!foreign_keys) return foreign_keys;
    return migrate();
}

Result<void> SqliteRepository::migrate() {
    auto begin = execute("BEGIN IMMEDIATE;");
    if (!begin) return begin;

    auto rollback = [this]() { (void)execute("ROLLBACK;"); };
    auto migration_table = execute(
        "CREATE TABLE IF NOT EXISTS schema_migrations("
        "version INTEGER PRIMARY KEY, applied_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP);");
    if (!migration_table) {
        rollback();
        return migration_table;
    }

    Statement version_query(db_, "SELECT COALESCE(MAX(version), 0) FROM schema_migrations;");
    if (!version_query.valid() || sqlite3_step(version_query.get()) != SQLITE_ROW) {
        rollback();
        return std::unexpected(database_error(db_, "sqlite_migration", "Could not read schema version"));
    }
    const auto current_version = sqlite3_column_int64(version_query.get(), 0);

    if (current_version < 1) {
        const char* migration_1 = R"SQL(
          CREATE TABLE downloads(
            id TEXT PRIMARY KEY,
            task_type TEXT NOT NULL DEFAULT 'http_single',
            url TEXT NOT NULL,
            filename TEXT NOT NULL,
            save_dir TEXT NOT NULL DEFAULT '',
            total_bytes INTEGER NOT NULL DEFAULT 0 CHECK(total_bytes >= 0),
            downloaded_bytes INTEGER NOT NULL DEFAULT 0 CHECK(downloaded_bytes >= 0),
            status TEXT NOT NULL,
            error_code TEXT NOT NULL DEFAULT '',
            error_message TEXT NOT NULL DEFAULT '',
            etag TEXT NOT NULL DEFAULT '',
            last_modified TEXT NOT NULL DEFAULT '',
            headers_json TEXT NOT NULL DEFAULT '{}',
            category_id TEXT,
            queue_id TEXT,
            queue_order INTEGER NOT NULL DEFAULT 0,
            max_connections INTEGER NOT NULL DEFAULT 8,
            speed_limit_bps INTEGER NOT NULL DEFAULT 0,
            extractor_origin_url TEXT NOT NULL DEFAULT '',
            video_format_id TEXT NOT NULL DEFAULT '',
            audio_format_id TEXT NOT NULL DEFAULT '',
            created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
            completed_at TEXT
          );

          CREATE TABLE download_streams(
            id TEXT PRIMARY KEY,
            download_id TEXT NOT NULL REFERENCES downloads(id) ON DELETE CASCADE,
            stream_role TEXT NOT NULL DEFAULT 'primary',
            url TEXT NOT NULL,
            temp_path TEXT NOT NULL DEFAULT '',
            total_bytes INTEGER NOT NULL DEFAULT 0 CHECK(total_bytes >= 0),
            downloaded_bytes INTEGER NOT NULL DEFAULT 0 CHECK(downloaded_bytes >= 0),
            protocol TEXT NOT NULL DEFAULT 'http',
            format_id TEXT NOT NULL DEFAULT '',
            headers_json TEXT NOT NULL DEFAULT '{}',
            status TEXT NOT NULL DEFAULT 'Added'
          );

          CREATE TABLE segments(
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            stream_id TEXT NOT NULL REFERENCES download_streams(id) ON DELETE CASCADE,
            segment_index INTEGER NOT NULL,
            start_offset INTEGER NOT NULL,
            end_offset INTEGER NOT NULL,
            downloaded_bytes INTEGER NOT NULL DEFAULT 0,
            status TEXT NOT NULL DEFAULT 'Pending',
            UNIQUE(stream_id, segment_index),
            CHECK(start_offset >= 0),
            CHECK(end_offset >= start_offset),
            CHECK(downloaded_bytes >= 0)
          );

          CREATE TABLE queues(
            id TEXT PRIMARY KEY,
            name TEXT NOT NULL,
            max_concurrent INTEGER NOT NULL DEFAULT 3,
            is_active INTEGER NOT NULL DEFAULT 1,
            schedule_enabled INTEGER NOT NULL DEFAULT 0,
            start_time TEXT,
            stop_time TEXT,
            days_mask INTEGER NOT NULL DEFAULT 127,
            on_complete_action TEXT NOT NULL DEFAULT 'none'
          );

          CREATE TABLE categories(
            id TEXT PRIMARY KEY,
            name TEXT NOT NULL,
            icon TEXT NOT NULL DEFAULT '',
            save_dir TEXT NOT NULL DEFAULT '',
            extensions_csv TEXT NOT NULL DEFAULT '',
            url_patterns_json TEXT NOT NULL DEFAULT '[]',
            is_default INTEGER NOT NULL DEFAULT 0
          );

          CREATE TABLE ytdlp_profiles(
            id TEXT PRIMARY KEY,
            name TEXT NOT NULL,
            source_type TEXT NOT NULL,
            repo_slug TEXT NOT NULL DEFAULT '',
            custom_api_url TEXT NOT NULL DEFAULT '',
            asset_regex TEXT NOT NULL DEFAULT '',
            pinned_tag TEXT NOT NULL DEFAULT '',
            include_prereleases INTEGER NOT NULL DEFAULT 0,
            local_binary_path TEXT NOT NULL DEFAULT '',
            python_path TEXT NOT NULL DEFAULT '',
            installed_version TEXT NOT NULL DEFAULT '',
            last_etag TEXT NOT NULL DEFAULT '',
            last_checked_at TEXT,
            is_active INTEGER NOT NULL DEFAULT 0
          );

          CREATE TABLE settings(
            key TEXT PRIMARY KEY,
            value_json TEXT NOT NULL,
            updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
          );

          CREATE INDEX idx_downloads_status ON downloads(status);
          CREATE INDEX idx_downloads_queue_order ON downloads(queue_id, queue_order);
          CREATE INDEX idx_streams_download_id ON download_streams(download_id);
          CREATE INDEX idx_segments_stream_id ON segments(stream_id);
        )SQL";
        auto schema = execute(migration_1);
        if (!schema) {
            rollback();
            return schema;
        }
        auto insert_version = execute("INSERT INTO schema_migrations(version) VALUES(1);");
        if (!insert_version) {
            rollback();
            return insert_version;
        }
    }

    if (current_version < 2) {
        auto default_queue = execute(
            "INSERT OR IGNORE INTO queues(id,name,max_concurrent,is_active) "
            "VALUES('default','Default',3,1);");
        if (!default_queue) {
            rollback();
            return default_queue;
        }
        auto insert_version = execute("INSERT INTO schema_migrations(version) VALUES(2);");
        if (!insert_version) {
            rollback();
            return insert_version;
        }
    }

    auto commit = execute("COMMIT;");
    if (!commit) {
        rollback();
        return commit;
    }
    return {};
}

Result<void> SqliteRepository::save_queue(const QueueRecord& record) {
    if(db_==nullptr)return std::unexpected(make_error("sqlite_open",open_error_));
    if(record.id.empty() || record.name.empty() || record.max_concurrent==0U || record.max_concurrent>64U)
        return std::unexpected(make_error("invalid_queue","Queue id/name and concurrency 1..64 are required"));
    Statement statement(db_,R"SQL(
      INSERT INTO queues(id,name,max_concurrent,is_active) VALUES(?,?,?,?)
      ON CONFLICT(id) DO UPDATE SET name=excluded.name,max_concurrent=excluded.max_concurrent,is_active=excluded.is_active;
    )SQL");
    if(!statement.valid())return std::unexpected(database_error(db_,"sqlite_prepare","Could not prepare queue upsert"));
    auto id=bind_text(statement.get(),1,record.id);if(!id)return id;
    auto name=bind_text(statement.get(),2,record.name);if(!name)return name;
    if(sqlite3_bind_int64(statement.get(),3,static_cast<sqlite3_int64>(record.max_concurrent))!=SQLITE_OK ||
       sqlite3_bind_int(statement.get(),4,record.is_active?1:0)!=SQLITE_OK)
        return std::unexpected(database_error(db_,"sqlite_bind","Could not bind queue settings"));
    if(sqlite3_step(statement.get())!=SQLITE_DONE)return std::unexpected(database_error(db_,"sqlite_write","Could not save queue"));
    return {};
}

Result<std::vector<QueueRecord>> SqliteRepository::list_queues() {
    if(db_==nullptr)return std::unexpected(make_error("sqlite_open",open_error_));
    Statement statement(db_,"SELECT id,name,max_concurrent,is_active FROM queues ORDER BY name,id;");
    if(!statement.valid())return std::unexpected(database_error(db_,"sqlite_prepare","Could not prepare queue query"));
    std::vector<QueueRecord> records;
    int rc=SQLITE_OK;
    while((rc=sqlite3_step(statement.get()))==SQLITE_ROW){
        const auto* id=reinterpret_cast<const char*>(sqlite3_column_text(statement.get(),0));
        const auto* name=reinterpret_cast<const char*>(sqlite3_column_text(statement.get(),1));
        QueueRecord record;
        record.id=id?id:"";record.name=name?name:"";
        record.max_concurrent=static_cast<std::uint64_t>(sqlite3_column_int64(statement.get(),2));
        record.is_active=sqlite3_column_int(statement.get(),3)!=0;
        records.push_back(std::move(record));
    }
    if(rc!=SQLITE_DONE)return std::unexpected(database_error(db_,"sqlite_read","Could not read queues"));
    return records;
}

Result<void> SqliteRepository::save_download(const DownloadRecord& record) {
    if (db_ == nullptr) return std::unexpected(make_error("sqlite_open", open_error_));
    if (record.id.empty() || record.url.empty() || record.filename.empty()) {
        return std::unexpected(make_error("invalid_download", "Download id, URL and filename are required"));
    }
    auto total = as_sqlite_integer(record.total_bytes, "total_bytes");
    auto downloaded = as_sqlite_integer(record.downloaded_bytes, "downloaded_bytes");
    if (!total) return std::unexpected(total.error());
    if (!downloaded) return std::unexpected(downloaded.error());

    Statement statement(db_, R"SQL(
      INSERT INTO downloads(id, task_type, url, filename, save_dir, queue_id, total_bytes, downloaded_bytes,
                            status, etag, last_modified, headers_json, extractor_origin_url,
                            video_format_id, audio_format_id)
      VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)
      ON CONFLICT(id) DO UPDATE SET
        task_type=excluded.task_type, url=excluded.url, filename=excluded.filename,
        save_dir=excluded.save_dir, queue_id=excluded.queue_id, total_bytes=excluded.total_bytes,
        downloaded_bytes=excluded.downloaded_bytes, status=excluded.status,
        etag=excluded.etag, last_modified=excluded.last_modified,
        headers_json=excluded.headers_json, extractor_origin_url=excluded.extractor_origin_url,
        video_format_id=excluded.video_format_id, audio_format_id=excluded.audio_format_id;
    )SQL");
    if (!statement.valid()) return std::unexpected(database_error(db_, "sqlite_prepare", "Could not prepare download upsert"));

    const std::string* values[] = {
        &record.id, &record.task_type, &record.url, &record.filename, &record.save_dir, &record.queue_id
    };
    for (int index = 0; index < 6; ++index) {
        auto result = bind_text(statement.get(), index + 1, *values[index]);
        if (!result) return result;
    }
    if (sqlite3_bind_int64(statement.get(), 7, *total) != SQLITE_OK ||
        sqlite3_bind_int64(statement.get(), 8, *downloaded) != SQLITE_OK) {
        return std::unexpected(database_error(db_, "sqlite_bind", "Could not bind download byte counts"));
    }
    const std::string* trailing[] = {
        &record.status, &record.etag, &record.last_modified, &record.headers_json,
        &record.extractor_origin_url, &record.video_format_id, &record.audio_format_id
    };
    for (int index = 0; index < 7; ++index) {
        auto result = bind_text(statement.get(), index + 9, *trailing[index]);
        if (!result) return result;
    }
    if (sqlite3_step(statement.get()) != SQLITE_DONE) {
        return std::unexpected(database_error(db_, "sqlite_write", "Could not save download"));
    }
    return {};
}

Result<void> SqliteRepository::save_stream(const StreamRecord& record) {
    if (db_ == nullptr) return std::unexpected(make_error("sqlite_open", open_error_));
    if (record.id.empty() || record.download_id.empty() || record.url.empty()) {
        return std::unexpected(make_error("invalid_stream", "Stream id, parent download id and URL are required"));
    }
    auto total = as_sqlite_integer(record.total_bytes, "stream total_bytes");
    auto downloaded = as_sqlite_integer(record.downloaded_bytes, "stream downloaded_bytes");
    if (!total) return std::unexpected(total.error());
    if (!downloaded) return std::unexpected(downloaded.error());

    Statement statement(db_, R"SQL(
      INSERT INTO download_streams(id, download_id, stream_role, url, temp_path, total_bytes,
                                   downloaded_bytes, protocol, format_id, headers_json, status)
      VALUES(?,?,?,?,?,?,?,?,?,?,?)
      ON CONFLICT(id) DO UPDATE SET
        download_id=excluded.download_id, stream_role=excluded.stream_role, url=excluded.url,
        temp_path=excluded.temp_path, total_bytes=excluded.total_bytes,
        downloaded_bytes=excluded.downloaded_bytes, protocol=excluded.protocol,
        format_id=excluded.format_id, headers_json=excluded.headers_json, status=excluded.status;
    )SQL");
    if (!statement.valid()) return std::unexpected(database_error(db_, "sqlite_prepare", "Could not prepare stream upsert"));

    const std::string* text_values[] = {
        &record.id, &record.download_id, &record.stream_role, &record.url, &record.temp_path
    };
    for (int index = 0; index < 5; ++index) {
        auto result = bind_text(statement.get(), index + 1, *text_values[index]);
        if (!result) return result;
    }
    if (sqlite3_bind_int64(statement.get(), 6, *total) != SQLITE_OK ||
        sqlite3_bind_int64(statement.get(), 7, *downloaded) != SQLITE_OK) {
        return std::unexpected(database_error(db_, "sqlite_bind", "Could not bind stream byte counts"));
    }
    const std::string* trailing[] = {&record.protocol, &record.format_id, &record.headers_json, &record.status};
    for (int index = 0; index < 4; ++index) {
        auto result = bind_text(statement.get(), index + 8, *trailing[index]);
        if (!result) return result;
    }
    if (sqlite3_step(statement.get()) != SQLITE_DONE) {
        return std::unexpected(database_error(db_, "sqlite_write", "Could not save stream"));
    }
    return {};
}

Result<void> SqliteRepository::save_segment_checkpoint(const SegmentRecord& record) {
    if (db_ == nullptr) return std::unexpected(make_error("sqlite_open", open_error_));
    if (record.stream_id.empty() || record.end_offset < record.start_offset ||
        record.downloaded_bytes > (record.end_offset - record.start_offset + 1U)) {
        return std::unexpected(make_error("invalid_segment", "Segment range or downloaded byte count is invalid"));
    }
    auto segment_index = as_sqlite_integer(record.segment_index, "segment_index");
    auto start = as_sqlite_integer(record.start_offset, "segment start_offset");
    auto end = as_sqlite_integer(record.end_offset, "segment end_offset");
    auto downloaded = as_sqlite_integer(record.downloaded_bytes, "segment downloaded_bytes");
    if (!segment_index) return std::unexpected(segment_index.error());
    if (!start) return std::unexpected(start.error());
    if (!end) return std::unexpected(end.error());
    if (!downloaded) return std::unexpected(downloaded.error());

    Statement statement(db_, R"SQL(
      INSERT INTO segments(stream_id, segment_index, start_offset, end_offset, downloaded_bytes, status)
      VALUES(?,?,?,?,?,?)
      ON CONFLICT(stream_id, segment_index) DO UPDATE SET
        start_offset=excluded.start_offset, end_offset=excluded.end_offset,
        downloaded_bytes=excluded.downloaded_bytes, status=excluded.status;
    )SQL");
    if (!statement.valid()) return std::unexpected(database_error(db_, "sqlite_prepare", "Could not prepare segment upsert"));
    auto stream = bind_text(statement.get(), 1, record.stream_id);
    if (!stream) return stream;
    if (sqlite3_bind_int64(statement.get(), 2, *segment_index) != SQLITE_OK ||
        sqlite3_bind_int64(statement.get(), 3, *start) != SQLITE_OK ||
        sqlite3_bind_int64(statement.get(), 4, *end) != SQLITE_OK ||
        sqlite3_bind_int64(statement.get(), 5, *downloaded) != SQLITE_OK) {
        return std::unexpected(database_error(db_, "sqlite_bind", "Could not bind segment range"));
    }
    auto status = bind_text(statement.get(), 6, record.status);
    if (!status) return status;
    if (sqlite3_step(statement.get()) != SQLITE_DONE) {
        return std::unexpected(database_error(db_, "sqlite_write", "Could not save segment checkpoint"));
    }
    return {};
}

Result<void> SqliteRepository::save_ytdlp_profile(const YtDlpProfileRecord& record) {
    if (db_ == nullptr) return std::unexpected(make_error("sqlite_open", open_error_));
    if (record.id.empty() || record.name.empty() || record.source_type.empty()) {
        return std::unexpected(make_error("invalid_ytdlp_profile", "Profile id, name and source type are required"));
    }
    Statement statement(db_, R"SQL(
      INSERT INTO ytdlp_profiles(id,name,source_type,repo_slug,custom_api_url,asset_regex,pinned_tag,
        include_prereleases,local_binary_path,python_path,installed_version,last_etag,is_active)
      VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?)
      ON CONFLICT(id) DO UPDATE SET name=excluded.name,source_type=excluded.source_type,
        repo_slug=excluded.repo_slug,custom_api_url=excluded.custom_api_url,asset_regex=excluded.asset_regex,
        pinned_tag=excluded.pinned_tag,include_prereleases=excluded.include_prereleases,
        local_binary_path=excluded.local_binary_path,python_path=excluded.python_path,
        installed_version=excluded.installed_version,last_etag=excluded.last_etag,is_active=excluded.is_active;
    )SQL");
    if (!statement.valid()) return std::unexpected(database_error(db_, "sqlite_prepare", "Could not prepare yt-dlp profile upsert"));
    const std::string* values[] = {&record.id,&record.name,&record.source_type,&record.repo_slug,
        &record.custom_api_url,&record.asset_regex,&record.pinned_tag};
    for (int index=0; index<7; ++index) {
        auto bound=bind_text(statement.get(),index+1,*values[index]);
        if(!bound)return bound;
    }
    if(sqlite3_bind_int(statement.get(),8,record.include_prereleases?1:0)!=SQLITE_OK)
        return std::unexpected(database_error(db_,"sqlite_bind","Could not bind profile prerelease flag"));
    const std::string* trailing[]={&record.local_binary_path,&record.python_path,&record.installed_version,&record.last_etag};
    for(int index=0;index<4;++index){
        auto bound=bind_text(statement.get(),index+9,*trailing[index]);
        if(!bound)return bound;
    }
    if(sqlite3_bind_int(statement.get(),13,record.is_active?1:0)!=SQLITE_OK)
        return std::unexpected(database_error(db_,"sqlite_bind","Could not bind profile active flag"));
    if(sqlite3_step(statement.get())!=SQLITE_DONE)
        return std::unexpected(database_error(db_,"sqlite_write","Could not save yt-dlp profile"));
    return {};
}

Result<std::vector<YtDlpProfileRecord>> SqliteRepository::list_ytdlp_profiles() {
    if(db_==nullptr)return std::unexpected(make_error("sqlite_open",open_error_));
    Statement statement(db_,R"SQL(
      SELECT id,name,source_type,repo_slug,custom_api_url,asset_regex,pinned_tag,include_prereleases,
             local_binary_path,python_path,installed_version,last_etag,is_active
      FROM ytdlp_profiles ORDER BY name,id;
    )SQL");
    if(!statement.valid())return std::unexpected(database_error(db_,"sqlite_prepare","Could not prepare yt-dlp profile query"));
    std::vector<YtDlpProfileRecord> records;
    int rc=SQLITE_OK;
    while((rc=sqlite3_step(statement.get()))==SQLITE_ROW){
        const auto text_column=[statement_ptr=statement.get()](int index){
            const auto* value=reinterpret_cast<const char*>(sqlite3_column_text(statement_ptr,index));
            return value?std::string(value):std::string{};
        };
        YtDlpProfileRecord record;
        record.id=text_column(0);record.name=text_column(1);record.source_type=text_column(2);
        record.repo_slug=text_column(3);record.custom_api_url=text_column(4);record.asset_regex=text_column(5);
        record.pinned_tag=text_column(6);record.include_prereleases=sqlite3_column_int(statement.get(),7)!=0;
        record.local_binary_path=text_column(8);record.python_path=text_column(9);
        record.installed_version=text_column(10);record.last_etag=text_column(11);
        record.is_active=sqlite3_column_int(statement.get(),12)!=0;
        records.push_back(std::move(record));
    }
    if(rc!=SQLITE_DONE)return std::unexpected(database_error(db_,"sqlite_read","Could not read yt-dlp profiles"));
    return records;
}

Result<std::vector<SegmentRecord>> SqliteRepository::list_segments(const std::string& stream_id) {
    if (db_ == nullptr) return std::unexpected(make_error("sqlite_open", open_error_));
    if (stream_id.empty()) return std::unexpected(make_error("invalid_stream", "Stream id is required"));
    Statement statement(db_, R"SQL(
      SELECT stream_id, segment_index, start_offset, end_offset, downloaded_bytes, status
      FROM segments WHERE stream_id=? ORDER BY segment_index;
    )SQL");
    if (!statement.valid()) return std::unexpected(database_error(db_, "sqlite_prepare", "Could not prepare segment recovery query"));
    auto bound = bind_text(statement.get(), 1, stream_id);
    if (!bound) return std::unexpected(bound.error());
    std::vector<SegmentRecord> records;
    int rc = SQLITE_OK;
    while ((rc = sqlite3_step(statement.get())) == SQLITE_ROW) {
        const auto* stream = reinterpret_cast<const char*>(sqlite3_column_text(statement.get(), 0));
        const auto* status = reinterpret_cast<const char*>(sqlite3_column_text(statement.get(), 5));
        SegmentRecord record;
        record.stream_id = stream != nullptr ? stream : "";
        record.segment_index = static_cast<std::uint64_t>(sqlite3_column_int64(statement.get(), 1));
        record.start_offset = static_cast<std::uint64_t>(sqlite3_column_int64(statement.get(), 2));
        record.end_offset = static_cast<std::uint64_t>(sqlite3_column_int64(statement.get(), 3));
        record.downloaded_bytes = static_cast<std::uint64_t>(sqlite3_column_int64(statement.get(), 4));
        record.status = status != nullptr ? status : "Pending";
        records.push_back(std::move(record));
    }
    if (rc != SQLITE_DONE) return std::unexpected(database_error(db_, "sqlite_read", "Could not read segment recovery journal"));
    return records;
}

Result<std::optional<DownloadRecord>> SqliteRepository::claim_next_queued_download() {
    if (db_ == nullptr) return std::unexpected(make_error("sqlite_open", open_error_));
    auto begin = execute("BEGIN IMMEDIATE;");
    if (!begin) return std::unexpected(begin.error());
    Statement statement(db_, R"SQL(
      SELECT id, task_type, url, filename, save_dir, queue_id, status, total_bytes, downloaded_bytes,
             etag, last_modified, headers_json, extractor_origin_url, video_format_id, audio_format_id
      FROM downloads WHERE status='Queued' ORDER BY created_at, id LIMIT 1;
    )SQL");
    if (!statement.valid()) {
        (void)execute("ROLLBACK;");
        return std::unexpected(database_error(db_, "sqlite_prepare", "Could not prepare queued download claim"));
    }
    const int rc = sqlite3_step(statement.get());
    if (rc == SQLITE_DONE) {
        auto committed = execute("COMMIT;");
        if (!committed) return std::unexpected(committed.error());
        return std::optional<DownloadRecord>{};
    }
    if (rc != SQLITE_ROW) {
        (void)execute("ROLLBACK;");
        return std::unexpected(database_error(db_, "sqlite_read", "Could not read next queued download"));
    }
    const auto text_column = [statement_ptr = statement.get()](int index) {
        const auto* value = reinterpret_cast<const char*>(sqlite3_column_text(statement_ptr, index));
        return value != nullptr ? std::string(value) : std::string{};
    };
    DownloadRecord record;
    record.id=text_column(0);record.task_type=text_column(1);record.url=text_column(2);
    record.filename=text_column(3);record.save_dir=text_column(4);record.queue_id=text_column(5);
    record.status="Downloading";
    record.total_bytes=static_cast<std::uint64_t>(sqlite3_column_int64(statement.get(),7));
    record.downloaded_bytes=static_cast<std::uint64_t>(sqlite3_column_int64(statement.get(),8));
    record.etag=text_column(9);record.last_modified=text_column(10);record.headers_json=text_column(11);
    record.extractor_origin_url=text_column(12);record.video_format_id=text_column(13);record.audio_format_id=text_column(14);
    (void)sqlite3_reset(statement.get());
    Statement claim(db_, "UPDATE downloads SET status='Downloading' WHERE id=? AND status='Queued';");
    if (!claim.valid()) {
        (void)execute("ROLLBACK;");
        return std::unexpected(database_error(db_, "sqlite_prepare", "Could not prepare queued download claim update"));
    }
    auto bound=bind_text(claim.get(),1,record.id);
    if(!bound){(void)execute("ROLLBACK;");return std::unexpected(bound.error());}
    if(sqlite3_step(claim.get())!=SQLITE_DONE || sqlite3_changes(db_)!=1){
        (void)execute("ROLLBACK;");
        return std::unexpected(database_error(db_,"sqlite_claim","Could not atomically claim queued download"));
    }
    auto committed=execute("COMMIT;");
    if(!committed){(void)execute("ROLLBACK;");return std::unexpected(committed.error());}
    return std::optional<DownloadRecord>{std::move(record)};
}

Result<std::string> SqliteRepository::get_download_status(const std::string& id) {
    if(db_==nullptr)return std::unexpected(make_error("sqlite_open",open_error_));
    Statement statement(db_,"SELECT status FROM downloads WHERE id=?;");
    if(!statement.valid())return std::unexpected(database_error(db_,"sqlite_prepare","Could not prepare download status query"));
    auto bound=bind_text(statement.get(),1,id);if(!bound)return std::unexpected(bound.error());
    const int rc=sqlite3_step(statement.get());
    if(rc==SQLITE_DONE)return std::unexpected(make_error("download_not_found","Download task does not exist"));
    if(rc!=SQLITE_ROW)return std::unexpected(database_error(db_,"sqlite_read","Could not read download status"));
    const auto* value=reinterpret_cast<const char*>(sqlite3_column_text(statement.get(),0));
    return value?std::string(value):std::string{};
}

Result<void> SqliteRepository::update_download_progress(
    const std::string& id, std::uint64_t downloaded, std::uint64_t total) {
    if(db_==nullptr)return std::unexpected(make_error("sqlite_open",open_error_));
    if(id.empty() || (total>0U && downloaded>total))
        return std::unexpected(make_error("invalid_progress","Download progress is outside the valid range"));
    const auto downloaded_value=as_sqlite_integer(downloaded);
    const auto total_value=as_sqlite_integer(total);
    if(!downloaded_value || !total_value)return std::unexpected(make_error("invalid_progress","Download progress exceeds SQLite integer range"));
    Statement statement(db_,"UPDATE downloads SET downloaded_bytes=?, total_bytes=? WHERE id=?;");
    if(!statement.valid())return std::unexpected(database_error(db_,"sqlite_prepare","Could not prepare download progress update"));
    if(sqlite3_bind_int64(statement.get(),1,*downloaded_value)!=SQLITE_OK ||
       sqlite3_bind_int64(statement.get(),2,*total_value)!=SQLITE_OK)
        return std::unexpected(database_error(db_,"sqlite_bind","Could not bind download progress"));
    auto bound=bind_text(statement.get(),3,id);if(!bound)return bound;
    if(sqlite3_step(statement.get())!=SQLITE_DONE)return std::unexpected(database_error(db_,"sqlite_write","Could not update download progress"));
    if(sqlite3_changes(db_)!=1)return std::unexpected(make_error("download_not_found","Download task does not exist"));
    return {};
}

Result<void> SqliteRepository::update_download_status(
    const std::string& id, const std::string& status, const std::string& error_message) {
    if(db_==nullptr)return std::unexpected(make_error("sqlite_open",open_error_));
    if(id.empty())return std::unexpected(make_error("invalid_download","Download id is required"));
    static const std::vector<std::string> allowed={"Added","Queued","Probing","Downloading","Paused","Verifying","Completed","Error","Cancelled"};
    if(std::find(allowed.begin(),allowed.end(),status)==allowed.end())
        return std::unexpected(make_error("invalid_status","Unknown download status"));
    Statement statement(db_,R"SQL(
      UPDATE downloads SET status=?, error_message=?, completed_at=CASE WHEN ?='Completed' THEN CURRENT_TIMESTAMP ELSE completed_at END
      WHERE id=?;
    )SQL");
    if(!statement.valid())return std::unexpected(database_error(db_,"sqlite_prepare","Could not prepare download status update"));
    auto s=bind_text(statement.get(),1,status);if(!s)return s;
    auto e=bind_text(statement.get(),2,error_message);if(!e)return e;
    auto status_again=bind_text(statement.get(),3,status);if(!status_again)return status_again;
    auto id_bound=bind_text(statement.get(),4,id);if(!id_bound)return id_bound;
    if(sqlite3_step(statement.get())!=SQLITE_DONE)return std::unexpected(database_error(db_,"sqlite_write","Could not update download status"));
    if(sqlite3_changes(db_)!=1)return std::unexpected(make_error("download_not_found","Download task does not exist"));
    return {};
}

Result<std::vector<DownloadRecord>> SqliteRepository::list_downloads() {
    if (db_ == nullptr) return std::unexpected(make_error("sqlite_open", open_error_));
    Statement statement(db_, R"SQL(
      SELECT id, task_type, url, filename, save_dir, queue_id, status, total_bytes, downloaded_bytes,
             etag, last_modified, headers_json, extractor_origin_url, video_format_id, audio_format_id
      FROM downloads ORDER BY created_at, id;
    )SQL");
    if (!statement.valid()) return std::unexpected(database_error(db_, "sqlite_prepare", "Could not prepare download query"));

    std::vector<DownloadRecord> records;
    int rc = SQLITE_OK;
    while ((rc = sqlite3_step(statement.get())) == SQLITE_ROW) {
        const auto text_column = [statement_ptr = statement.get()](int index) {
            const auto* value = reinterpret_cast<const char*>(sqlite3_column_text(statement_ptr, index));
            return value != nullptr ? std::string(value) : std::string{};
        };
        DownloadRecord record;
        record.id = text_column(0);
        record.task_type = text_column(1);
        record.url = text_column(2);
        record.filename = text_column(3);
        record.save_dir = text_column(4);
        record.queue_id = text_column(5);
        record.status = text_column(6);
        record.total_bytes = static_cast<std::uint64_t>(sqlite3_column_int64(statement.get(), 7));
        record.downloaded_bytes = static_cast<std::uint64_t>(sqlite3_column_int64(statement.get(), 8));
        record.etag = text_column(9);
        record.last_modified = text_column(10);
        record.headers_json = text_column(11);
        record.extractor_origin_url = text_column(12);
        record.video_format_id = text_column(13);
        record.audio_format_id = text_column(14);
        records.push_back(std::move(record));
    }
    if (rc != SQLITE_DONE) return std::unexpected(database_error(db_, "sqlite_read", "Could not read downloads"));
    return records;
}

Result<std::uint64_t> SqliteRepository::schema_version() {
    if (db_ == nullptr) return std::unexpected(make_error("sqlite_open", open_error_));
    Statement statement(db_, "SELECT COALESCE(MAX(version), 0) FROM schema_migrations;");
    if (!statement.valid() || sqlite3_step(statement.get()) != SQLITE_ROW) {
        return std::unexpected(database_error(db_, "sqlite_migration", "Could not read schema version"));
    }
    const auto version = sqlite3_column_int64(statement.get(), 0);
    return static_cast<std::uint64_t>(version);
}

} // namespace vortex
