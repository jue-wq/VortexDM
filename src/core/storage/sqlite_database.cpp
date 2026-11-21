#include "sqlite_database.hpp"

#include "core/download/download_state_machine.hpp"

#include <sqlite3.h>

#include <limits>
#include <string>
#include <utility>

namespace vortex {
namespace {

class Statement final {
public:
    Statement(sqlite3* db, const char* sql) {
        result_ = sqlite3_prepare_v2(db, sql, -1, &statement_, nullptr);
    }
    ~Statement() { if (statement_) sqlite3_finalize(statement_); }
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
    [[nodiscard]] int result() const { return result_; }
    [[nodiscard]] sqlite3_stmt* get() const { return statement_; }
private:
    sqlite3_stmt* statement_{};
    int result_{};
};

[[nodiscard]] bool valid_state(const std::int32_t value) {
    return value >= static_cast<std::int32_t>(DownloadState::queued) &&
           value <= static_cast<std::int32_t>(DownloadState::cancelled);
}

} // namespace

SqliteDatabase::~SqliteDatabase() { (void)close(); }

Status SqliteDatabase::open(const std::filesystem::path& path) {
    std::scoped_lock lock(mutex_);
    if (db_) {
        return std::unexpected(VortexError::make(
            ErrorCode::database_error, "Database is already open"
        ));
    }
    const auto native = path.string();
    const int rc = sqlite3_open_v2(
        native.c_str(), &db_,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
        nullptr
    );
    if (rc != SQLITE_OK) {
        auto error = database_error("Unable to open SQLite database");
        if (db_) sqlite3_close_v2(std::exchange(db_, nullptr));
        return std::unexpected(std::move(error));
    }
    sqlite3_busy_timeout(db_, 5000);
    const auto journal = execute("PRAGMA journal_mode=WAL;");
    if (!journal) return journal;
    return execute("PRAGMA foreign_keys=ON;");
}

Status SqliteDatabase::close() {
    std::scoped_lock lock(mutex_);
    if (!db_) return {};
    const int rc = sqlite3_close(db_);
    if (rc != SQLITE_OK) {
        return std::unexpected(database_error("Unable to close SQLite database"));
    }
    db_ = nullptr;
    return {};
}

Status SqliteDatabase::execute(const char* sql) {
    if (!db_) return std::unexpected(VortexError::make(
        ErrorCode::database_error, "Database is not open"
    ));
    char* message = nullptr;
    const int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &message);
    if (rc != SQLITE_OK) {
        std::string detail = message ? message : sqlite3_errmsg(db_);
        sqlite3_free(message);
        return std::unexpected(VortexError::make(
            ErrorCode::database_error, "SQLite statement failed: " + detail
        ));
    }
    return {};
}

VortexError SqliteDatabase::database_error(const char* context) const {
    const std::string detail = db_ ? sqlite3_errmsg(db_) : "no database handle";
    return VortexError::make(ErrorCode::database_error,
                             std::string(context) + ": " + detail);
}

Status SqliteDatabase::migrate() {
    std::scoped_lock lock(mutex_);
    if (!db_) return std::unexpected(VortexError::make(
        ErrorCode::database_error, "Database is not open"
    ));
    auto status = execute("BEGIN IMMEDIATE;");
    if (!status) return status;
    status = execute(
        "CREATE TABLE IF NOT EXISTS schema_migrations ("
        "version INTEGER PRIMARY KEY NOT NULL, applied_at INTEGER NOT NULL);"
        "CREATE TABLE IF NOT EXISTS downloads ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "url TEXT NOT NULL,"
        "destination TEXT NOT NULL,"
        "total_bytes INTEGER NOT NULL DEFAULT -1 CHECK(total_bytes >= -1),"
        "downloaded_bytes INTEGER NOT NULL DEFAULT 0 CHECK(downloaded_bytes >= 0),"
        "state INTEGER NOT NULL DEFAULT 0 CHECK(state BETWEEN 0 AND 5),"
        "checksum_sha256 TEXT NOT NULL DEFAULT '',"
        "created_at INTEGER NOT NULL,"
        "updated_at INTEGER NOT NULL,"
        "CHECK(total_bytes = -1 OR downloaded_bytes <= total_bytes));"
        "CREATE INDEX IF NOT EXISTS idx_downloads_state_updated "
        "ON downloads(state, updated_at DESC);"
        "INSERT OR IGNORE INTO schema_migrations(version, applied_at) "
        "VALUES(1, unixepoch());"
    );
    if (!status) {
        (void)execute("ROLLBACK;");
        return status;
    }
    status = execute(
        "CREATE TABLE IF NOT EXISTS download_segments ("
        "download_id INTEGER NOT NULL REFERENCES downloads(id) ON DELETE CASCADE,"
        "segment_index INTEGER NOT NULL CHECK(segment_index >= 0),"
        "byte_offset INTEGER NOT NULL CHECK(byte_offset >= 0),"
        "byte_length INTEGER NOT NULL CHECK(byte_length > 0),"
        "checksum_sha256 TEXT NOT NULL DEFAULT '',"
        "updated_at INTEGER NOT NULL,"
        "PRIMARY KEY(download_id, segment_index));"
        "CREATE INDEX IF NOT EXISTS idx_download_segments_download "
        "ON download_segments(download_id, segment_index);"
        "INSERT OR IGNORE INTO schema_migrations(version, applied_at) "
        "VALUES(2, unixepoch());"
    );
    if (!status) {
        (void)execute("ROLLBACK;");
        return status;
    }
    status = execute("COMMIT;");
    if (!status) (void)execute("ROLLBACK;");
    return status;
}


Status SqliteDatabase::recover_interrupted_downloads(const std::int64_t updated_at_unix) {
    std::scoped_lock lock(mutex_);
    if (!db_) return std::unexpected(VortexError::make(
        ErrorCode::database_error, "Database is not open"
    ));
    auto status = execute("BEGIN IMMEDIATE;");
    if (!status) return status;

    Statement statement(db_,
        "UPDATE downloads SET state=?, updated_at=? WHERE state=?;");
    if (statement.result() != SQLITE_OK) {
        (void)execute("ROLLBACK;");
        return std::unexpected(database_error("Prepare interrupted download recovery"));
    }
    sqlite3_bind_int(statement.get(), 1, static_cast<int>(DownloadState::paused));
    sqlite3_bind_int64(statement.get(), 2, updated_at_unix);
    sqlite3_bind_int(statement.get(), 3, static_cast<int>(DownloadState::running));
    if (sqlite3_step(statement.get()) != SQLITE_DONE) {
        auto error = database_error("Recover interrupted downloads");
        (void)execute("ROLLBACK;");
        return std::unexpected(std::move(error));
    }
    status = execute("COMMIT;");
    if (!status) (void)execute("ROLLBACK;");
    return status;
}

Result<std::int64_t> SqliteDatabase::insert_download(const DownloadRecord& record) {
    std::scoped_lock lock(mutex_);
    if (!db_) return std::unexpected(VortexError::make(
        ErrorCode::database_error, "Database is not open"
    ));
    if (record.url.empty() || record.destination.empty() ||
        record.total_bytes < -1 || record.downloaded_bytes < 0 ||
        (record.total_bytes >= 0 && record.downloaded_bytes > record.total_bytes)) {
        return std::unexpected(VortexError::make(
            ErrorCode::invalid_argument, "Invalid download record"
        ));
    }
    Statement statement(db_,
        "INSERT INTO downloads(url,destination,total_bytes,downloaded_bytes,state,"
        "checksum_sha256,created_at,updated_at) VALUES(?,?,?,?,?,?,?,?);");
    if (statement.result() != SQLITE_OK) return std::unexpected(database_error("Prepare insert"));
    auto* query = statement.get();
    sqlite3_bind_text(query, 1, record.url.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(query, 2, record.destination.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(query, 3, record.total_bytes);
    sqlite3_bind_int64(query, 4, record.downloaded_bytes);
    sqlite3_bind_int(query, 5, static_cast<int>(record.state));
    sqlite3_bind_text(query, 6, record.checksum_sha256.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(query, 7, record.created_at_unix);
    sqlite3_bind_int64(query, 8, record.updated_at_unix);
    if (sqlite3_step(query) != SQLITE_DONE) return std::unexpected(database_error("Insert download"));
    return sqlite3_last_insert_rowid(db_);
}

Status SqliteDatabase::update_download_progress(
    const std::int64_t id, const std::int64_t downloaded_bytes,
    const DownloadState state, const std::int64_t updated_at_unix
) {
    std::scoped_lock lock(mutex_);
    if (!db_) return std::unexpected(VortexError::make(ErrorCode::database_error, "Database is not open"));
    if (id <= 0 || downloaded_bytes < 0 ||
        !valid_state(static_cast<std::int32_t>(state))) {
        return std::unexpected(VortexError::make(ErrorCode::invalid_argument, "Invalid progress update"));
    }
    int current_value = -1;
    {
        Statement current(db_, "SELECT state FROM downloads WHERE id=?;");
        if (current.result() != SQLITE_OK) return std::unexpected(database_error("Prepare current download state"));
        sqlite3_bind_int64(current.get(), 1, id);
        const int current_result = sqlite3_step(current.get());
        if (current_result == SQLITE_DONE) return std::unexpected(VortexError::make(
            ErrorCode::invalid_argument, "Download does not exist"));
        if (current_result != SQLITE_ROW) return std::unexpected(database_error("Read current download state"));
        current_value = sqlite3_column_int(current.get(), 0);
    }
    if (!valid_state(current_value)) return std::unexpected(VortexError::make(
        ErrorCode::database_error, "Stored download state is invalid"));
    const auto current_state = static_cast<DownloadState>(current_value);
    if (current_state != state) {
        const auto transition = DownloadStateMachine::validate_transition(current_state, state);
        if (!transition) return transition;
    }
    // Compare the observed state in the UPDATE to reject cross-connection races.
    Statement statement(db_,
        "UPDATE downloads SET downloaded_bytes=?, state=?, updated_at=? "
        "WHERE id=? AND state=? AND (total_bytes=-1 OR ?<=total_bytes);");
    if (statement.result() != SQLITE_OK) return std::unexpected(database_error("Prepare progress update"));
    auto* query = statement.get();
    sqlite3_bind_int64(query, 1, downloaded_bytes);
    sqlite3_bind_int(query, 2, static_cast<int>(state));
    sqlite3_bind_int64(query, 3, updated_at_unix);
    sqlite3_bind_int64(query, 4, id);
    sqlite3_bind_int(query, 5, current_value);
    sqlite3_bind_int64(query, 6, downloaded_bytes);
    if (sqlite3_step(query) != SQLITE_DONE) return std::unexpected(database_error("Update progress"));
    if (sqlite3_changes(db_) == 0) return std::unexpected(VortexError::make(
        ErrorCode::invalid_argument, "Download state changed concurrently or progress exceeds total size"));
    return {};
}

Status SqliteDatabase::retry_failed_download(
    const std::int64_t id, const std::int64_t updated_at_unix
) {
    std::scoped_lock lock(mutex_);
    if (!db_) return std::unexpected(VortexError::make(
        ErrorCode::database_error, "Database is not open"
    ));
    if (id <= 0) return std::unexpected(VortexError::make(
        ErrorCode::invalid_argument, "Invalid download id"
    ));

    auto status = execute("BEGIN IMMEDIATE;");
    if (!status) return status;

    Statement queue(db_,
        "UPDATE downloads SET state=?, updated_at=? WHERE id=? AND state=?;");
    if (queue.result() != SQLITE_OK) {
        (void)execute("ROLLBACK;");
        return std::unexpected(database_error("Prepare failed download retry"));
    }
    sqlite3_bind_int(queue.get(), 1, static_cast<int>(DownloadState::queued));
    sqlite3_bind_int64(queue.get(), 2, updated_at_unix);
    sqlite3_bind_int64(queue.get(), 3, id);
    sqlite3_bind_int(queue.get(), 4, static_cast<int>(DownloadState::failed));
    if (sqlite3_step(queue.get()) != SQLITE_DONE) {
        auto error = database_error("Queue failed download retry");
        (void)execute("ROLLBACK;");
        return std::unexpected(std::move(error));
    }
    if (sqlite3_changes(db_) != 1) {
        (void)execute("ROLLBACK;");
        return std::unexpected(VortexError::make(
            ErrorCode::invalid_argument, "Only failed downloads can be retried"
        ));
    }

    Statement run(db_,
        "UPDATE downloads SET state=?, updated_at=? WHERE id=? AND state=?;");
    if (run.result() != SQLITE_OK) {
        (void)execute("ROLLBACK;");
        return std::unexpected(database_error("Prepare resumed failed download"));
    }
    sqlite3_bind_int(run.get(), 1, static_cast<int>(DownloadState::running));
    sqlite3_bind_int64(run.get(), 2, updated_at_unix);
    sqlite3_bind_int64(run.get(), 3, id);
    sqlite3_bind_int(run.get(), 4, static_cast<int>(DownloadState::queued));
    if (sqlite3_step(run.get()) != SQLITE_DONE) {
        auto error = database_error("Start failed download retry");
        (void)execute("ROLLBACK;");
        return std::unexpected(std::move(error));
    }
    if (sqlite3_changes(db_) != 1) {
        (void)execute("ROLLBACK;");
        return std::unexpected(VortexError::make(
            ErrorCode::database_error, "Failed download retry lost its queued state"
        ));
    }

    status = execute("COMMIT;");
    if (!status) (void)execute("ROLLBACK;");
    return status;
}

Result<std::vector<DownloadRecord>> SqliteDatabase::list_downloads() {
    std::scoped_lock lock(mutex_);
    if (!db_) return std::unexpected(VortexError::make(ErrorCode::database_error, "Database is not open"));
    Statement statement(db_,
        "SELECT id,url,destination,total_bytes,downloaded_bytes,state,checksum_sha256,"
        "created_at,updated_at FROM downloads ORDER BY created_at DESC,id DESC;");
    if (statement.result() != SQLITE_OK) return std::unexpected(database_error("Prepare list"));
    std::vector<DownloadRecord> records;
    for (;;) {
        const int rc = sqlite3_step(statement.get());
        if (rc == SQLITE_DONE) break;
        if (rc != SQLITE_ROW) return std::unexpected(database_error("Read downloads"));
        const auto state = sqlite3_column_int(statement.get(), 5);
        if (!valid_state(state)) return std::unexpected(VortexError::make(
            ErrorCode::database_error, "Stored download state is invalid"
        ));
        const auto* url = sqlite3_column_text(statement.get(), 1);
        const auto* destination = sqlite3_column_text(statement.get(), 2);
        const auto* checksum = sqlite3_column_text(statement.get(), 6);
        records.push_back(DownloadRecord{
            sqlite3_column_int64(statement.get(), 0),
            url ? reinterpret_cast<const char*>(url) : "",
            destination ? reinterpret_cast<const char*>(destination) : "",
            sqlite3_column_int64(statement.get(), 3),
            sqlite3_column_int64(statement.get(), 4),
            static_cast<DownloadState>(state),
            checksum ? reinterpret_cast<const char*>(checksum) : "",
            sqlite3_column_int64(statement.get(), 7),
            sqlite3_column_int64(statement.get(), 8)
        });
    }
    return records;
}

Status SqliteDatabase::delete_download(const std::int64_t id) {
    std::scoped_lock lock(mutex_);
    if (!db_) return std::unexpected(VortexError::make(ErrorCode::database_error, "Database is not open"));
    if (id <= 0) return std::unexpected(VortexError::make(ErrorCode::invalid_argument, "Invalid download id"));
    Statement statement(db_, "DELETE FROM downloads WHERE id=?;");
    if (statement.result() != SQLITE_OK) return std::unexpected(database_error("Prepare delete"));
    sqlite3_bind_int64(statement.get(), 1, id);
    if (sqlite3_step(statement.get()) != SQLITE_DONE) return std::unexpected(database_error("Delete download"));
    if (sqlite3_changes(db_) == 0) return std::unexpected(VortexError::make(
        ErrorCode::invalid_argument, "Download does not exist"
    ));
    return {};
}


Status SqliteDatabase::record_segment_checkpoint(const SegmentCheckpoint& checkpoint) {
    std::scoped_lock lock(mutex_);
    if (!db_) return std::unexpected(VortexError::make(
        ErrorCode::database_error, "Database is not open"
    ));
    if (checkpoint.download_id <= 0 || checkpoint.segment_index < 0 ||
        checkpoint.offset < 0 || checkpoint.length <= 0 ||
        checkpoint.offset > std::numeric_limits<std::int64_t>::max() - checkpoint.length) {
        return std::unexpected(VortexError::make(
            ErrorCode::invalid_argument, "Invalid segment checkpoint"
        ));
    }
    Statement statement(db_,
        "INSERT INTO download_segments(download_id,segment_index,byte_offset,byte_length,"
        "checksum_sha256,updated_at) "
        "SELECT ?,?,?,?,?,? WHERE EXISTS ("
        "SELECT 1 FROM downloads WHERE id=? AND "
        "(total_bytes=-1 OR (? + ?) <= total_bytes) "
        "AND NOT EXISTS (SELECT 1 FROM download_segments s WHERE "
        "s.download_id=? AND s.byte_offset < (? + ?) "
        "AND ? < (s.byte_offset + s.byte_length)));");
    if (statement.result() != SQLITE_OK) {
        return std::unexpected(database_error("Prepare segment checkpoint"));
    }
    auto* query = statement.get();
    sqlite3_bind_int64(query, 1, checkpoint.download_id);
    sqlite3_bind_int(query, 2, checkpoint.segment_index);
    sqlite3_bind_int64(query, 3, checkpoint.offset);
    sqlite3_bind_int64(query, 4, checkpoint.length);
    sqlite3_bind_text(query, 5, checkpoint.checksum_sha256.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(query, 6, checkpoint.updated_at_unix);
    sqlite3_bind_int64(query, 7, checkpoint.download_id);
    sqlite3_bind_int64(query, 8, checkpoint.offset);
    sqlite3_bind_int64(query, 9, checkpoint.length);
    sqlite3_bind_int64(query, 10, checkpoint.download_id);
    sqlite3_bind_int64(query, 11, checkpoint.offset);
    sqlite3_bind_int64(query, 12, checkpoint.length);
    sqlite3_bind_int64(query, 13, checkpoint.offset);
    if (sqlite3_step(query) != SQLITE_DONE) {
        return std::unexpected(database_error("Insert segment checkpoint"));
    }
    if (sqlite3_changes(db_) == 0) {
        return std::unexpected(VortexError::make(
            ErrorCode::invalid_argument,
            "Download does not exist or checkpoint exceeds the known file size"
        ));
    }
    return {};
}

Result<std::vector<SegmentCheckpoint>> SqliteDatabase::list_segment_checkpoints(
    const std::int64_t download_id
) {
    std::scoped_lock lock(mutex_);
    if (!db_) return std::unexpected(VortexError::make(
        ErrorCode::database_error, "Database is not open"
    ));
    if (download_id <= 0) return std::unexpected(VortexError::make(
        ErrorCode::invalid_argument, "Invalid download id"
    ));
    Statement statement(db_,
        "SELECT download_id,segment_index,byte_offset,byte_length,checksum_sha256,updated_at "
        "FROM download_segments WHERE download_id=? ORDER BY segment_index;");
    if (statement.result() != SQLITE_OK) {
        return std::unexpected(database_error("Prepare segment checkpoint list"));
    }
    sqlite3_bind_int64(statement.get(), 1, download_id);
    std::vector<SegmentCheckpoint> checkpoints;
    for (;;) {
        const int rc = sqlite3_step(statement.get());
        if (rc == SQLITE_DONE) break;
        if (rc != SQLITE_ROW) {
            return std::unexpected(database_error("Read segment checkpoints"));
        }
        const auto* checksum = sqlite3_column_text(statement.get(), 4);
        checkpoints.push_back(SegmentCheckpoint{
            sqlite3_column_int64(statement.get(), 0),
            sqlite3_column_int(statement.get(), 1),
            sqlite3_column_int64(statement.get(), 2),
            sqlite3_column_int64(statement.get(), 3),
            checksum ? reinterpret_cast<const char*>(checksum) : "",
            sqlite3_column_int64(statement.get(), 5)
        });
    }
    return checkpoints;
}

Status SqliteDatabase::clear_segment_checkpoints(const std::int64_t download_id) {
    std::scoped_lock lock(mutex_);
    if (!db_) return std::unexpected(VortexError::make(
        ErrorCode::database_error, "Database is not open"
    ));
    if (download_id <= 0) return std::unexpected(VortexError::make(
        ErrorCode::invalid_argument, "Invalid download id"
    ));
    Statement statement(db_, "DELETE FROM download_segments WHERE download_id=?;");
    if (statement.result() != SQLITE_OK) {
        return std::unexpected(database_error("Prepare segment checkpoint clear"));
    }
    sqlite3_bind_int64(statement.get(), 1, download_id);
    if (sqlite3_step(statement.get()) != SQLITE_DONE) {
        return std::unexpected(database_error("Clear segment checkpoints"));
    }
    return {};
}


Status SqliteDatabase::erase_segment_checkpoint(
    const std::int64_t download_id, const std::int32_t segment_index
) {
    std::scoped_lock lock(mutex_);
    if (!db_) return std::unexpected(VortexError::make(
        ErrorCode::database_error, "Database is not open"
    ));
    if (download_id <= 0 || segment_index < 0) {
        return std::unexpected(VortexError::make(
            ErrorCode::invalid_argument, "Invalid segment checkpoint identity"
        ));
    }
    Statement statement(db_,
        "DELETE FROM download_segments WHERE download_id=? AND segment_index=?;");
    if (statement.result() != SQLITE_OK) {
        return std::unexpected(database_error("Prepare segment checkpoint erase"));
    }
    sqlite3_bind_int64(statement.get(), 1, download_id);
    sqlite3_bind_int(statement.get(), 2, segment_index);
    if (sqlite3_step(statement.get()) != SQLITE_DONE) {
        return std::unexpected(database_error("Erase segment checkpoint"));
    }
    if (sqlite3_changes(db_) == 0) {
        return std::unexpected(VortexError::make(
            ErrorCode::invalid_argument, "Segment checkpoint does not exist"
        ));
    }
    return {};
}

} // namespace vortex
