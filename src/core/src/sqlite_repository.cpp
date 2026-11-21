#include "vortex/sqlite_repository.hpp"
#include <string>
namespace vortex {
SqliteRepository::SqliteRepository(const std::filesystem::path& path) {
    sqlite3_open_v2(path.string().c_str(), &db_, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr);
}
SqliteRepository::~SqliteRepository() { if (db_) sqlite3_close(db_); }
Result<void> SqliteRepository::initialize() {
    if (!db_) return std::unexpected(make_error("sqlite_open", "Could not open SQLite database"));
    const char* sql = R"SQL(
      PRAGMA journal_mode=WAL;
      PRAGMA synchronous=NORMAL;
      PRAGMA foreign_keys=ON;
      CREATE TABLE IF NOT EXISTS schema_migrations(version INTEGER PRIMARY KEY, applied_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP);
      CREATE TABLE IF NOT EXISTS downloads(
        id TEXT PRIMARY KEY, url TEXT NOT NULL, filename TEXT NOT NULL, status TEXT NOT NULL,
        total_bytes INTEGER NOT NULL DEFAULT 0, downloaded_bytes INTEGER NOT NULL DEFAULT 0,
        created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
      );
    )SQL";
    char* message = nullptr;
    const int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &message);
    if (rc != SQLITE_OK) {
        std::string detail = message ? message : sqlite3_errmsg(db_); sqlite3_free(message);
        return std::unexpected(make_error("sqlite_schema", detail));
    }
    return {};
}
} // namespace vortex
