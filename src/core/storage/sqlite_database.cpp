#include "sqlite_database.hpp"
#include "core/download/download_state_machine.hpp"
#include "core/download/http_response_headers.hpp"

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

[[nodiscard]] bool valid_request_id(const std::string_view value) {
    if (value.empty() || value.size() > 128U) return false;
    for (const unsigned char character : value) {
        if (character < 0x21U || character > 0x7eU) return false;
    }
    return true;
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
        "etag TEXT NOT NULL DEFAULT '',"
        "CHECK(total_bytes = -1 OR downloaded_bytes <= total_bytes));"
        "CREATE INDEX IF NOT EXISTS idx_downloads_state_updated "
        "ON downloads(state, updated_at DESC);"
        "CREATE TABLE IF NOT EXISTS native_request_dedup ("
        "request_id TEXT PRIMARY KEY NOT NULL CHECK(length(request_id) BETWEEN 1 AND 128),"
        "download_id INTEGER NOT NULL UNIQUE REFERENCES downloads(id) ON DELETE CASCADE,"
        "created_at INTEGER NOT NULL DEFAULT (unixepoch()));"
        "CREATE TABLE IF NOT EXISTS native_request_delivery ("
        "request_id TEXT PRIMARY KEY NOT NULL REFERENCES native_request_dedup(request_id) ON DELETE CASCADE,"
        "imported_at INTEGER NOT NULL DEFAULT (unixepoch()));"
        "INSERT OR IGNORE INTO schema_migrations(version, applied_at) "
        "VALUES(1, unixepoch());"
        "INSERT OR IGNORE INTO schema_migrations(version, applied_at) "
        "VALUES(2, unixepoch());"
    );
    if (!status) {
        (void)execute("ROLLBACK;");
        return status;
    }
    bool has_etag_column = false;
    {
        Statement columns(db_, "PRAGMA table_info(downloads);");
        if (columns.result() != SQLITE_OK) {
            (void)execute("ROLLBACK;");
            return std::unexpected(database_error("Inspect downloads schema"));
        }
        for (;;) {
            const int rc = sqlite3_step(columns.get());
            if (rc == SQLITE_DONE) break;
            if (rc != SQLITE_ROW) {
                auto error = database_error("Read downloads schema");
                (void)execute("ROLLBACK;");
                return std::unexpected(std::move(error));
            }
            const auto* name = sqlite3_column_text(columns.get(), 1);
            if (name && std::string_view(reinterpret_cast<const char*>(name)) == "etag") {
                has_etag_column = true;
            }
        }
    }
    if (!has_etag_column) {
        status = execute("ALTER TABLE downloads ADD COLUMN etag TEXT NOT NULL DEFAULT ''; ");
        if (!status) {
            (void)execute("ROLLBACK;");
            return status;
        }
    }
    status = execute("INSERT OR IGNORE INTO schema_migrations(version, applied_at) VALUES(3, unixepoch());");
    if (!status) {
        (void)execute("ROLLBACK;");
        return status;
    }
    status = execute("COMMIT;");
    if (!status) (void)execute("ROLLBACK;");
    return status;
}

Result<std::optional<DownloadRecord>> SqliteDatabase::claim_next_queued_download(
    const std::int64_t updated_at_unix
) {
    std::scoped_lock lock(mutex_);
    if (!db_) return std::unexpected(VortexError::make(
        ErrorCode::database_error, "Database is not open"
    ));

    auto status = execute("BEGIN IMMEDIATE;");
    if (!status) return std::unexpected(status.error());

    std::optional<DownloadRecord> claimed;
    {
        Statement select(db_,
            "SELECT id,url,destination,total_bytes,downloaded_bytes,state,checksum_sha256,"
            "created_at,etag FROM downloads WHERE state=? "
            "ORDER BY created_at,id LIMIT 1;");
        if (select.result() != SQLITE_OK) {
            auto error = database_error("Prepare queued download claim");
            (void)execute("ROLLBACK;");
            return std::unexpected(std::move(error));
        }
        sqlite3_bind_int(select.get(), 1, static_cast<int>(DownloadState::queued));
        const int rc = sqlite3_step(select.get());
        if (rc == SQLITE_ROW) {
            const auto state = sqlite3_column_int(select.get(), 5);
            if (!valid_state(state) || state != static_cast<int>(DownloadState::queued)) {
                (void)execute("ROLLBACK;");
                return std::unexpected(VortexError::make(
                    ErrorCode::database_error, "Queued download has an invalid state"
                ));
            }
            const auto* url = sqlite3_column_text(select.get(), 1);
            const auto* destination = sqlite3_column_text(select.get(), 2);
            const auto* checksum = sqlite3_column_text(select.get(), 6);
            claimed.emplace(DownloadRecord{
                sqlite3_column_int64(select.get(), 0),
                url ? reinterpret_cast<const char*>(url) : "",
                destination ? reinterpret_cast<const char*>(destination) : "",
                sqlite3_column_int64(select.get(), 3),
                sqlite3_column_int64(select.get(), 4),
                DownloadState::running,
                checksum ? reinterpret_cast<const char*>(checksum) : "",
                sqlite3_column_int64(select.get(), 7),
                updated_at_unix,
                sqlite3_column_text(select.get(), 8)
                    ? reinterpret_cast<const char*>(sqlite3_column_text(select.get(), 8)) : ""
            });
        } else if (rc != SQLITE_DONE) {
            auto error = database_error("Read queued download claim");
            (void)execute("ROLLBACK;");
            return std::unexpected(std::move(error));
        }
    }

    if (!claimed) {
        status = execute("COMMIT;");
        if (!status) {
            (void)execute("ROLLBACK;");
            return std::unexpected(status.error());
        }
        return std::optional<DownloadRecord>{};
    }

    Statement update(db_,
        "UPDATE downloads SET state=?,updated_at=? WHERE id=? AND state=?;");
    if (update.result() != SQLITE_OK) {
        auto error = database_error("Prepare queued download claim update");
        (void)execute("ROLLBACK;");
        return std::unexpected(std::move(error));
    }
    sqlite3_bind_int(update.get(), 1, static_cast<int>(DownloadState::running));
    sqlite3_bind_int64(update.get(), 2, updated_at_unix);
    sqlite3_bind_int64(update.get(), 3, claimed->id);
    sqlite3_bind_int(update.get(), 4, static_cast<int>(DownloadState::queued));
    if (sqlite3_step(update.get()) != SQLITE_DONE || sqlite3_changes(db_) != 1) {
        auto error = database_error("Claim queued download");
        (void)execute("ROLLBACK;");
        return std::unexpected(std::move(error));
    }

    status = execute("COMMIT;");
    if (!status) {
        (void)execute("ROLLBACK;");
        return std::unexpected(status.error());
    }
    return claimed;
}

Result<std::int64_t> SqliteDatabase::insert_download(const DownloadRecord& record) {
    std::scoped_lock lock(mutex_);
    if (!db_) return std::unexpected(VortexError::make(
        ErrorCode::database_error, "Database is not open"
    ));
    if (record.url.empty() || record.destination.empty() ||
        record.total_bytes < -1 || record.downloaded_bytes < 0 ||
        (record.total_bytes >= 0 && record.downloaded_bytes > record.total_bytes) ||
        !valid_state(static_cast<std::int32_t>(record.state)) ||
        (!record.etag.empty() && !is_strong_etag(record.etag))) {
        return std::unexpected(VortexError::make(
            ErrorCode::invalid_argument, "Invalid download record"
        ));
    }
    Statement statement(db_,
        "INSERT INTO downloads(url,destination,total_bytes,downloaded_bytes,state,"
        "checksum_sha256,created_at,updated_at,etag) VALUES(?,?,?,?,?,?,?,?,?);");
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
    sqlite3_bind_text(query, 9, record.etag.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(query) != SQLITE_DONE) return std::unexpected(database_error("Insert download"));
    return sqlite3_last_insert_rowid(db_);
}


Result<std::int64_t> SqliteDatabase::enqueue_download_once(
    const std::string_view request_id, const DownloadRecord& record
) {
    std::scoped_lock lock(mutex_);
    if (!db_) return std::unexpected(VortexError::make(
        ErrorCode::database_error, "Database is not open"
    ));
    if (!valid_request_id(request_id) ||
        record.url.empty() || record.destination.empty() ||
        record.total_bytes < -1 || record.downloaded_bytes < 0 ||
        (record.total_bytes >= 0 && record.downloaded_bytes > record.total_bytes) ||
        !valid_state(static_cast<std::int32_t>(record.state))) {
        return std::unexpected(VortexError::make(
            ErrorCode::invalid_argument, "Invalid request ID or download record"
        ));
    }

    auto status = execute("BEGIN IMMEDIATE;");
    if (!status) return std::unexpected(status.error());

    Statement existing(db_,
        "SELECT d.id,d.url,d.destination FROM native_request_dedup r "
        "JOIN downloads d ON d.id=r.download_id WHERE r.request_id=?;");
    if (existing.result() != SQLITE_OK) {
        (void)execute("ROLLBACK;");
        return std::unexpected(database_error("Prepare native request lookup"));
    }
    sqlite3_bind_text(existing.get(), 1, request_id.data(),
                      static_cast<int>(request_id.size()), SQLITE_TRANSIENT);
    const int lookup_rc = sqlite3_step(existing.get());
    if (lookup_rc == SQLITE_ROW) {
        const auto id = sqlite3_column_int64(existing.get(), 0);
        const auto* stored_url = sqlite3_column_text(existing.get(), 1);
        const auto* stored_destination = sqlite3_column_text(existing.get(), 2);
        const std::string existing_url = stored_url
            ? reinterpret_cast<const char*>(stored_url) : "";
        const std::string existing_destination = stored_destination
            ? reinterpret_cast<const char*>(stored_destination) : "";
        if (existing_url != record.url || existing_destination != record.destination) {
            (void)execute("ROLLBACK;");
            return std::unexpected(VortexError::make(
                ErrorCode::invalid_argument,
                "Request ID was already used for a different download payload"
            ));
        }
        status = execute("COMMIT;");
        if (!status) {
            (void)execute("ROLLBACK;");
            return std::unexpected(status.error());
        }
        return id;
    }
    if (lookup_rc != SQLITE_DONE) {
        auto error = database_error("Lookup native request");
        (void)execute("ROLLBACK;");
        return std::unexpected(std::move(error));
    }

    Statement insert(db_,
        "INSERT INTO downloads(url,destination,total_bytes,downloaded_bytes,state,"
        "checksum_sha256,created_at,updated_at) VALUES(?,?,?,?,?,?,?,?);");
    if (insert.result() != SQLITE_OK) {
        auto error = database_error("Prepare native download insert");
        (void)execute("ROLLBACK;");
        return std::unexpected(std::move(error));
    }
    auto* query = insert.get();
    sqlite3_bind_text(query, 1, record.url.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(query, 2, record.destination.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(query, 3, record.total_bytes);
    sqlite3_bind_int64(query, 4, record.downloaded_bytes);
    sqlite3_bind_int(query, 5, static_cast<int>(record.state));
    sqlite3_bind_text(query, 6, record.checksum_sha256.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(query, 7, record.created_at_unix);
    sqlite3_bind_int64(query, 8, record.updated_at_unix);
    if (sqlite3_step(query) != SQLITE_DONE) {
        auto error = database_error("Insert native download");
        (void)execute("ROLLBACK;");
        return std::unexpected(std::move(error));
    }
    const auto download_id = sqlite3_last_insert_rowid(db_);

    Statement bind_request(db_,
        "INSERT INTO native_request_dedup(request_id,download_id) VALUES(?,?);");
    if (bind_request.result() != SQLITE_OK) {
        auto error = database_error("Prepare native request binding");
        (void)execute("ROLLBACK;");
        return std::unexpected(std::move(error));
    }
    sqlite3_bind_text(bind_request.get(), 1, request_id.data(),
                      static_cast<int>(request_id.size()), SQLITE_TRANSIENT);
    sqlite3_bind_int64(bind_request.get(), 2, download_id);
    if (sqlite3_step(bind_request.get()) != SQLITE_DONE) {
        auto error = database_error("Bind native request ID");
        (void)execute("ROLLBACK;");
        return std::unexpected(std::move(error));
    }
    status = execute("COMMIT;");
    if (!status) {
        (void)execute("ROLLBACK;");
        return std::unexpected(status.error());
    }
    return download_id;
}

Result<std::vector<NativeInboxEntry>> SqliteDatabase::list_pending_native_requests(
    const std::size_t limit
) {
    std::scoped_lock lock(mutex_);
    if (!db_) return std::unexpected(VortexError::make(
        ErrorCode::database_error, "Database is not open"
    ));
    if (limit == 0U || limit > 1000U) return std::unexpected(VortexError::make(
        ErrorCode::invalid_argument, "Native inbox page size must be between 1 and 1000"
    ));
    Statement statement(db_,
        "SELECT r.request_id,d.id,d.url,d.destination,d.total_bytes,d.downloaded_bytes,"
        "d.state,d.checksum_sha256,d.created_at,d.updated_at,d.etag "
        "FROM native_request_dedup r JOIN downloads d ON d.id=r.download_id "
        "LEFT JOIN native_request_delivery delivered ON delivered.request_id=r.request_id "
        "WHERE delivered.request_id IS NULL ORDER BY r.created_at,r.request_id LIMIT ?;");
    if (statement.result() != SQLITE_OK) return std::unexpected(database_error("Prepare pending native inbox list"));
    sqlite3_bind_int64(statement.get(), 1, static_cast<sqlite3_int64>(limit));
    std::vector<NativeInboxEntry> entries;
    for (;;) {
        const int rc = sqlite3_step(statement.get());
        if (rc == SQLITE_DONE) break;
        if (rc != SQLITE_ROW) return std::unexpected(database_error("Read pending native inbox"));
        const auto state = sqlite3_column_int(statement.get(), 6);
        if (!valid_state(state)) return std::unexpected(VortexError::make(
            ErrorCode::database_error, "Stored native inbox download state is invalid"
        ));
        const auto* request_id = sqlite3_column_text(statement.get(), 0);
        const auto* url = sqlite3_column_text(statement.get(), 2);
        const auto* destination = sqlite3_column_text(statement.get(), 3);
        const auto* checksum = sqlite3_column_text(statement.get(), 7);
        entries.push_back(NativeInboxEntry{
            request_id ? reinterpret_cast<const char*>(request_id) : "",
            DownloadRecord{
                sqlite3_column_int64(statement.get(), 1),
                url ? reinterpret_cast<const char*>(url) : "",
                destination ? reinterpret_cast<const char*>(destination) : "",
                sqlite3_column_int64(statement.get(), 4),
                sqlite3_column_int64(statement.get(), 5),
                static_cast<DownloadState>(state),
                checksum ? reinterpret_cast<const char*>(checksum) : "",
                sqlite3_column_int64(statement.get(), 8),
                sqlite3_column_int64(statement.get(), 9),
                sqlite3_column_text(statement.get(), 10)
                    ? reinterpret_cast<const char*>(sqlite3_column_text(statement.get(), 10)) : ""
            }
        });
    }
    return entries;
}

Status SqliteDatabase::mark_native_request_imported(const std::string_view request_id) {
    std::scoped_lock lock(mutex_);
    if (!db_) return std::unexpected(VortexError::make(
        ErrorCode::database_error, "Database is not open"
    ));
    if (!valid_request_id(request_id)) return std::unexpected(VortexError::make(
        ErrorCode::invalid_argument, "Invalid native request ID"
    ));
    Statement statement(db_,
        "INSERT OR IGNORE INTO native_request_delivery(request_id) "
        "SELECT request_id FROM native_request_dedup WHERE request_id=?;");
    if (statement.result() != SQLITE_OK) return std::unexpected(database_error("Prepare native inbox delivery mark"));
    sqlite3_bind_text(statement.get(), 1, request_id.data(),
                      static_cast<int>(request_id.size()), SQLITE_TRANSIENT);
    if (sqlite3_step(statement.get()) != SQLITE_DONE) return std::unexpected(database_error("Mark native request imported"));
    if (sqlite3_changes(db_) != 0) return {};

    Statement verify(db_,
        "SELECT 1 FROM native_request_delivery WHERE request_id=?;");
    if (verify.result() != SQLITE_OK) return std::unexpected(database_error("Prepare native delivery verification"));
    sqlite3_bind_text(verify.get(), 1, request_id.data(),
                      static_cast<int>(request_id.size()), SQLITE_TRANSIENT);
    const int verify_rc = sqlite3_step(verify.get());
    if (verify_rc == SQLITE_ROW) return {}; // Another importer already committed the delivery mark.
    if (verify_rc != SQLITE_DONE) return std::unexpected(database_error("Verify native request delivery"));
    return std::unexpected(VortexError::make(
        ErrorCode::invalid_argument, "Native request does not exist"
    ));
}

Status SqliteDatabase::transition_download_state(
    const std::int64_t id, const DownloadState expected_from,
    const DownloadState to, const std::int64_t updated_at_unix
) {
    std::scoped_lock lock(mutex_);
    if (!db_) return std::unexpected(VortexError::make(
        ErrorCode::database_error, "Database is not open"
    ));
    if (id <= 0 || !valid_state(static_cast<std::int32_t>(expected_from)) ||
        !valid_state(static_cast<std::int32_t>(to))) {
        return std::unexpected(VortexError::make(
            ErrorCode::invalid_argument, "Invalid download state transition arguments"
        ));
    }
    const auto allowed = DownloadStateMachine::validate_transition(expected_from, to);
    if (!allowed) return std::unexpected(allowed.error());

    Statement statement(db_,
        "UPDATE downloads SET state=?, updated_at=? WHERE id=? AND state=?;");
    if (statement.result() != SQLITE_OK) return std::unexpected(
        database_error("Prepare state transition"));
    sqlite3_bind_int(statement.get(), 1, static_cast<int>(to));
    sqlite3_bind_int64(statement.get(), 2, updated_at_unix);
    sqlite3_bind_int64(statement.get(), 3, id);
    sqlite3_bind_int(statement.get(), 4, static_cast<int>(expected_from));
    if (sqlite3_step(statement.get()) != SQLITE_DONE) return std::unexpected(
        database_error("Transition download state"));
    if (sqlite3_changes(db_) == 0) return std::unexpected(VortexError::make(
        ErrorCode::invalid_argument,
        "Download does not exist or its state changed concurrently"
    ));
    return {};
}

Status SqliteDatabase::update_download_validator(
    const std::int64_t id, const std::string_view etag
) {
    std::scoped_lock lock(mutex_);
    if (!db_) return std::unexpected(VortexError::make(
        ErrorCode::database_error, "Database is not open"));
    if (id <= 0 || (!etag.empty() && !is_strong_etag(etag))) {
        return std::unexpected(VortexError::make(
            ErrorCode::invalid_argument, "Validator must be an empty value or a strong ETag"));
    }
    Statement statement(db_, "UPDATE downloads SET etag=? WHERE id=?;");
    if (statement.result() != SQLITE_OK) return std::unexpected(
        database_error("Prepare download validator update"));
    sqlite3_bind_text(statement.get(), 1, etag.data(),
                      static_cast<int>(etag.size()), SQLITE_TRANSIENT);
    sqlite3_bind_int64(statement.get(), 2, id);
    if (sqlite3_step(statement.get()) != SQLITE_DONE) return std::unexpected(
        database_error("Update download validator"));
    if (sqlite3_changes(db_) == 0) return std::unexpected(VortexError::make(
        ErrorCode::invalid_argument, "Download does not exist"));
    return {};
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
        "created_at,updated_at,etag FROM downloads ORDER BY created_at DESC,id DESC;");
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
            sqlite3_column_int64(statement.get(), 8),
            sqlite3_column_text(statement.get(), 9)
                ? reinterpret_cast<const char*>(sqlite3_column_text(statement.get(), 9)) : ""
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
