#include "sqlite_database.hpp"

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
    Statement statement(db_,
        "UPDATE downloads SET downloaded_bytes=?, state=?, updated_at=? "
        "WHERE id=? AND (total_bytes=-1 OR ?<=total_bytes);");
    if (statement.result() != SQLITE_OK) return std::unexpected(database_error("Prepare progress update"));
    auto* query = statement.get();
    sqlite3_bind_int64(query, 1, downloaded_bytes);
    sqlite3_bind_int(query, 2, static_cast<int>(state));
    sqlite3_bind_int64(query, 3, updated_at_unix);
    sqlite3_bind_int64(query, 4, id);
    sqlite3_bind_int64(query, 5, downloaded_bytes);
    if (sqlite3_step(query) != SQLITE_DONE) return std::unexpected(database_error("Update progress"));
    if (sqlite3_changes(db_) == 0) return std::unexpected(VortexError::make(
        ErrorCode::invalid_argument, "Download does not exist or progress exceeds total size"
    ));
    return {};
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

} // namespace vortex
