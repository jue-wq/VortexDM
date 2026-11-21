#pragma once
#include "vortex/error.hpp"
#include <filesystem>
#include <sqlite3.h>
namespace vortex {
class SqliteRepository {
public:
    explicit SqliteRepository(const std::filesystem::path& path);
    ~SqliteRepository();
    SqliteRepository(const SqliteRepository&) = delete;
    SqliteRepository& operator=(const SqliteRepository&) = delete;
    [[nodiscard]] Result<void> initialize();
private:
    sqlite3* db_{nullptr};
};
} // namespace vortex
