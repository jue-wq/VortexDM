#include "core/storage/sqlite_database.hpp"
#include "core/download/download_task_controller.hpp"

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
    REQUIRE(database.update_download_progress(*id, 128, vortex::DownloadState::running, 11).has_value());
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

TEST_CASE("SQLite recovery pauses tasks left running after an unclean shutdown") {
    TemporaryDatabase temporary;
    std::int64_t id{};
    {
        vortex::SqliteDatabase database;
        REQUIRE(database.open(temporary.path()).has_value());
        REQUIRE(database.migrate().has_value());
        vortex::DownloadRecord record;
        record.url = "https://example.test/recovery.bin";
        record.destination = "recovery.bin";
        record.total_bytes = 4096;
        record.downloaded_bytes = 1024;
        record.state = vortex::DownloadState::running;
        record.created_at_unix = 100;
        record.updated_at_unix = 101;
        const auto inserted = database.insert_download(record);
        REQUIRE(inserted);
        id = *inserted;
        REQUIRE(database.close().has_value());
    }
    {
        vortex::SqliteDatabase database;
        REQUIRE(database.open(temporary.path()).has_value());
        REQUIRE(database.migrate().has_value());
        REQUIRE(database.recover_interrupted_downloads(200).has_value());
        const auto rows = database.list_downloads();
        REQUIRE(rows);
        REQUIRE(rows->size() == 1);
        CHECK(rows->front().id == id);
        CHECK(rows->front().state == vortex::DownloadState::paused);
        CHECK(rows->front().downloaded_bytes == 1024);
        CHECK(rows->front().updated_at_unix == 200);
    }
}

TEST_CASE("SQLite recovery leaves non-running tasks unchanged") {
    TemporaryDatabase temporary;
    vortex::SqliteDatabase database;
    REQUIRE(database.open(temporary.path()).has_value());
    REQUIRE(database.migrate().has_value());
    vortex::DownloadRecord record;
    record.url = "https://example.test/queued.bin";
    record.destination = "queued.bin";
    record.state = vortex::DownloadState::queued;
    const auto id = database.insert_download(record);
    REQUIRE(id);
    REQUIRE(database.recover_interrupted_downloads(300).has_value());
    const auto rows = database.list_downloads();
    REQUIRE(rows);
    REQUIRE(rows->size() == 1);
    CHECK(rows->front().state == vortex::DownloadState::queued);
    CHECK(rows->front().updated_at_unix == record.updated_at_unix);
}


TEST_CASE("SQLite persists bounded segment checkpoints across reopen") {
    TemporaryDatabase temporary;
    std::int64_t download_id{};
    {
        vortex::SqliteDatabase database;
        REQUIRE(database.open(temporary.path()).has_value());
        REQUIRE(database.migrate().has_value());
        vortex::DownloadRecord record;
        record.url = "https://example.test/segments.bin";
        record.destination = "segments.bin";
        record.total_bytes = 8192;
        const auto inserted = database.insert_download(record);
        REQUIRE(inserted);
        download_id = *inserted;

        REQUIRE(database.record_segment_checkpoint(vortex::SegmentCheckpoint{
            download_id, 1, 4096, 4096, "sha256-second", 150
        }).has_value());
        REQUIRE(database.record_segment_checkpoint(vortex::SegmentCheckpoint{
            download_id, 0, 0, 4096, "sha256-first", 140
        }).has_value());
        CHECK_FALSE(database.record_segment_checkpoint(vortex::SegmentCheckpoint{
            download_id, 0, 0, 4096, "duplicate", 160
        }).has_value());
        CHECK_FALSE(database.record_segment_checkpoint(vortex::SegmentCheckpoint{
            download_id, 2, 8192, 1, "", 160
        }).has_value());
        CHECK_FALSE(database.record_segment_checkpoint(vortex::SegmentCheckpoint{
            download_id, 2, 2048, 4096, "overlap", 160
        }).has_value());
        CHECK_FALSE(database.record_segment_checkpoint(vortex::SegmentCheckpoint{
            download_id, 3, 0, 0, "", 160
        }).has_value());
        REQUIRE(database.close().has_value());
    }
    {
        vortex::SqliteDatabase database;
        REQUIRE(database.open(temporary.path()).has_value());
        REQUIRE(database.migrate().has_value());
        const auto checkpoints = database.list_segment_checkpoints(download_id);
        REQUIRE(checkpoints);
        REQUIRE(checkpoints->size() == 2);
        CHECK(checkpoints->at(0).segment_index == 0);
        CHECK(checkpoints->at(0).checksum_sha256 == "sha256-first");
        CHECK(checkpoints->at(1).offset == 4096);
        REQUIRE(database.erase_segment_checkpoint(download_id, 0).has_value());
        const auto remaining = database.list_segment_checkpoints(download_id);
        REQUIRE(remaining);
        REQUIRE(remaining->size() == 1);
        CHECK(remaining->front().segment_index == 1);
        CHECK_FALSE(database.erase_segment_checkpoint(download_id, 0).has_value());
        CHECK_FALSE(database.erase_segment_checkpoint(download_id, -1).has_value());
        REQUIRE(database.clear_segment_checkpoints(download_id).has_value());
        const auto empty = database.list_segment_checkpoints(download_id);
        REQUIRE(empty);
        CHECK(empty->empty());
    }
}

TEST_CASE("SQLite removes segment checkpoints when a download is deleted") {
    TemporaryDatabase temporary;
    vortex::SqliteDatabase database;
    REQUIRE(database.open(temporary.path()).has_value());
    REQUIRE(database.migrate().has_value());
    vortex::DownloadRecord record;
    record.url = "https://example.test/cascade.bin";
    record.destination = "cascade.bin";
    record.total_bytes = 16;
    const auto id = database.insert_download(record);
    REQUIRE(id);
    REQUIRE(database.record_segment_checkpoint(vortex::SegmentCheckpoint{
        *id, 0, 0, 16, "", 1
    }).has_value());
    REQUIRE(database.delete_download(*id).has_value());
    const auto checkpoints = database.list_segment_checkpoints(*id);
    REQUIRE(checkpoints);
    CHECK(checkpoints->empty());
}


TEST_CASE("SQLite enforces legal download state transitions") {
    TemporaryDatabase temporary;
    vortex::SqliteDatabase database;
    REQUIRE(database.open(temporary.path()).has_value());
    REQUIRE(database.migrate().has_value());
    vortex::DownloadRecord record;
    record.url = "https://example.test/state.bin";
    record.destination = "state.bin";
    record.total_bytes = 100;
    const auto id = database.insert_download(record);
    REQUIRE(id);
    CHECK_FALSE(database.update_download_progress(*id, 0, vortex::DownloadState::completed, 1).has_value());
    REQUIRE(database.update_download_progress(*id, 0, vortex::DownloadState::running, 2).has_value());
    REQUIRE(database.update_download_progress(*id, 25, vortex::DownloadState::running, 3).has_value());
    REQUIRE(database.update_download_progress(*id, 25, vortex::DownloadState::paused, 4).has_value());
    CHECK_FALSE(database.update_download_progress(*id, 25, vortex::DownloadState::completed, 5).has_value());
    const auto rows = database.list_downloads();
    REQUIRE(rows);
    REQUIRE(rows->size() == 1);
    CHECK(rows->front().state == vortex::DownloadState::paused);
    CHECK(rows->front().downloaded_bytes == 25);
}


TEST_CASE("Download task controller persists lifecycle and signals cancellation") {
    TemporaryDatabase temporary;
    vortex::SqliteDatabase database;
    REQUIRE(database.open(temporary.path()).has_value());
    REQUIRE(database.migrate().has_value());

    vortex::DownloadRecord record;
    record.url = "https://example.test/task.bin";
    record.destination = "task.bin";
    record.total_bytes = 100;
    const auto id = database.insert_download(record);
    REQUIRE(id);

    vortex::DownloadTaskController controller(database);
    REQUIRE(controller.start(*id, 1).has_value());
    auto token = controller.stop_token(*id);
    CHECK_FALSE(token.stop_requested());
    REQUIRE(controller.update_progress(*id, 40, 2).has_value());
    REQUIRE(controller.pause(*id, 3).has_value());
    CHECK(token.stop_requested());
    CHECK_FALSE(controller.update_progress(*id, 50, 4).has_value());

    REQUIRE(controller.resume(*id, 5).has_value());
    CHECK_FALSE(controller.stop_token(*id).stop_requested());
    REQUIRE(controller.update_progress(*id, 100, 6).has_value());
    REQUIRE(controller.complete(*id, 7).has_value());
    CHECK_FALSE(controller.resume(*id, 8).has_value());

    const auto rows = database.list_downloads();
    REQUIRE(rows);
    REQUIRE(rows->size() == 1);
    CHECK(rows->front().state == vortex::DownloadState::completed);
    CHECK(rows->front().downloaded_bytes == 100);
}


TEST_CASE("Download task controller retries failed tasks with a fresh stop source") {
    TemporaryDatabase temporary;
    vortex::SqliteDatabase database;
    REQUIRE(database.open(temporary.path()).has_value());
    REQUIRE(database.migrate().has_value());

    vortex::DownloadRecord record;
    record.url = "https://example.test/retry.bin";
    record.destination = "retry.bin";
    record.total_bytes = 200;
    const auto id = database.insert_download(record);
    REQUIRE(id);

    vortex::DownloadTaskController controller(database);
    REQUIRE(controller.start(*id, 10).has_value());
    const auto first_token = controller.stop_token(*id);
    REQUIRE(controller.update_progress(*id, 75, 11).has_value());
    REQUIRE(controller.fail(*id, 12).has_value());
    CHECK(first_token.stop_requested());

    REQUIRE(controller.retry(*id, 13).has_value());
    const auto retry_token = controller.stop_token(*id);
    CHECK_FALSE(retry_token.stop_requested());
    CHECK_FALSE(controller.retry(*id, 14).has_value());
    REQUIRE(controller.update_progress(*id, 90, 15).has_value());

    const auto rows = database.list_downloads();
    REQUIRE(rows);
    REQUIRE(rows->size() == 1);
    CHECK(rows->front().state == vortex::DownloadState::running);
    CHECK(rows->front().downloaded_bytes == 90);
}

TEST_CASE("Download task controller cancellation stops the active transfer") {
    TemporaryDatabase temporary;
    vortex::SqliteDatabase database;
    REQUIRE(database.open(temporary.path()).has_value());
    REQUIRE(database.migrate().has_value());

    vortex::DownloadRecord record;
    record.url = "https://example.test/cancel.bin";
    record.destination = "cancel.bin";
    record.total_bytes = 200;
    const auto id = database.insert_download(record);
    REQUIRE(id);

    vortex::DownloadTaskController controller(database);
    REQUIRE(controller.start(*id, 20).has_value());
    const auto token = controller.stop_token(*id);
    REQUIRE(controller.cancel(*id, 21).has_value());
    CHECK(token.stop_requested());
    CHECK_FALSE(controller.update_progress(*id, 1, 22).has_value());

    const auto rows = database.list_downloads();
    REQUIRE(rows);
    REQUIRE(rows->size() == 1);
    CHECK(rows->front().state == vortex::DownloadState::cancelled);
}
