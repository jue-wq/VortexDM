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


TEST_CASE("SQLite native request IDs make enqueue durable and idempotent") {
    TemporaryDatabase temporary;
    vortex::SqliteDatabase database;
    REQUIRE(database.open(temporary.path()).has_value());
    REQUIRE(database.migrate().has_value());

    vortex::DownloadRecord record;
    record.url = "https://example.test/native.bin";
    record.destination = "native.bin";
    record.total_bytes = 42;
    record.created_at_unix = 20;
    record.updated_at_unix = 20;

    const auto first = database.enqueue_download_once("native-request-001", record);
    REQUIRE(first);
    const auto replay = database.enqueue_download_once("native-request-001", record);
    REQUIRE(replay);
    CHECK(*replay == *first);

    auto conflicting_replay = record;
    conflicting_replay.url = "https://attacker.example/other.bin";
    CHECK_FALSE(database.enqueue_download_once("native-request-001", conflicting_replay).has_value());

    const auto rows = database.list_downloads();
    REQUIRE(rows);
    CHECK(rows->size() == 1);

    CHECK_FALSE(database.enqueue_download_once("", record).has_value());
    CHECK_FALSE(database.enqueue_download_once(std::string(129, 'x'), record).has_value());
}

TEST_CASE("SQLite native request deduplication survives reopening the database") {
    TemporaryDatabase temporary;
    vortex::DownloadRecord record;
    record.url = "https://example.test/persistent.bin";
    record.destination = "persistent.bin";
    record.created_at_unix = 30;
    record.updated_at_unix = 30;

    std::int64_t original_id{};
    {
        vortex::SqliteDatabase database;
        REQUIRE(database.open(temporary.path()).has_value());
        REQUIRE(database.migrate().has_value());
        const auto inserted = database.enqueue_download_once("persistent-request", record);
        REQUIRE(inserted);
        original_id = *inserted;
        REQUIRE(database.close().has_value());
    }
    {
        vortex::SqliteDatabase database;
        REQUIRE(database.open(temporary.path()).has_value());
        REQUIRE(database.migrate().has_value());
        const auto replay = database.enqueue_download_once("persistent-request", record);
        REQUIRE(replay);
        CHECK(*replay == original_id);
        const auto rows = database.list_downloads();
        REQUIRE(rows);
        CHECK(rows->size() == 1);
    }
}


TEST_CASE("SQLite storage rejects invalid states and non-printable native request IDs") {
    TemporaryDatabase temporary;
    vortex::SqliteDatabase database;
    REQUIRE(database.open(temporary.path()).has_value());
    REQUIRE(database.migrate().has_value());

    vortex::DownloadRecord invalid_state;
    invalid_state.url = "https://example.test/invalid-state.bin";
    invalid_state.destination = "invalid-state.bin";
    invalid_state.state = static_cast<vortex::DownloadState>(99);
    CHECK_FALSE(database.insert_download(invalid_state).has_value());
    CHECK_FALSE(database.enqueue_download_once("valid-request", invalid_state).has_value());

    vortex::DownloadRecord valid;
    valid.url = "https://example.test/valid.bin";
    valid.destination = "valid.bin";
    CHECK_FALSE(database.enqueue_download_once(std::string("bad\0id", 6), valid).has_value());
    CHECK_FALSE(database.enqueue_download_once("bad\nrequest", valid).has_value());

    const auto rows = database.list_downloads();
    REQUIRE(rows);
    CHECK(rows->empty());
}
