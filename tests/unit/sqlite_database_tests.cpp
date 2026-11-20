#include "core/storage/sqlite_database.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>

#include <filesystem>
#include <string>

namespace {
class TemporaryDatabase final {
public:
    TemporaryDatabase() : path_(std::filesystem::temp_directory_path() /
        ("vortexdm-storage-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()) + ".sqlite")) {}
    ~TemporaryDatabase() {
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
        std::filesystem::remove(path_.string() + "-wal", ignored);
        std::filesystem::remove(path_.string() + "-shm", ignored);
    }
    const std::filesystem::path& path() const { return path_; }
private:
    std::filesystem::path path_;
};
}

TEST_CASE("SQLite storage migrates and round-trips download records") {
    TemporaryDatabase temporary;
    vortex::SqliteDatabase database;
    REQUIRE(database.open(temporary.path()).has_value());
    REQUIRE(database.migrate().has_value());

    vortex::DownloadRecord record;
    record.url = "https://example.test/file.bin";
    record.destination = "/tmp/file.bin";
    record.total_bytes = 1000;
    record.downloaded_bytes = 128;
    record.created_at_unix = 10;
    record.updated_at_unix = 11;

    const auto id = database.insert_download(record);
    REQUIRE(id);
    REQUIRE(*id > 0);
    REQUIRE(database.update_download_progress(*id, 256, vortex::DownloadState::paused, 12).has_value());

    const auto rows = database.list_downloads();
    REQUIRE(rows);
    REQUIRE(rows->size() == 1);
    CHECK(rows->front().url == record.url);
    CHECK(rows->front().downloaded_bytes == 256);
    CHECK(rows->front().state == vortex::DownloadState::paused);

    REQUIRE(database.delete_download(*id).has_value());
    const auto empty = database.list_downloads();
    REQUIRE(empty);
    CHECK(empty->empty());
}

TEST_CASE("SQLite storage rejects invalid progress and records") {
    TemporaryDatabase temporary;
    vortex::SqliteDatabase database;
    REQUIRE(database.open(temporary.path()).has_value());
    REQUIRE(database.migrate().has_value());

    vortex::DownloadRecord invalid;
    invalid.url = "";
    invalid.destination = "out.bin";
    CHECK_FALSE(database.insert_download(invalid).has_value());

    vortex::DownloadRecord record;
    record.url = "https://example.test/file.bin";
    record.destination = "out.bin";
    record.total_bytes = 10;
    const auto id = database.insert_download(record);
    REQUIRE(id);
    CHECK_FALSE(database.update_download_progress(*id, 11, vortex::DownloadState::running, 1).has_value());
}
