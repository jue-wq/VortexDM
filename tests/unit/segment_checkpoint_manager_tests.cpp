#include "core/common/crypto_utils.hpp"
#include "core/storage/segment_checkpoint_manager.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <filesystem>
#include <string>

namespace {
std::filesystem::path unique_path() {
    return std::filesystem::temp_directory_path() /
        ("vortexdm-checkpoint-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()) + ".part");
}
class TemporaryDatabase final {
public:
    TemporaryDatabase() : path_(unique_path().string() + ".sqlite") {}
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

TEST_CASE("Segment checkpoint is recorded only after durable write and digest verification") {
    const auto path = unique_path();
    TemporaryDatabase temporary;
    vortex::SqliteDatabase database;
    REQUIRE(database.open(temporary.path()).has_value());
    REQUIRE(database.migrate().has_value());
    vortex::DownloadRecord record;
    record.url = "https://example.test/checkpoint.bin";
    record.destination = path.string();
    record.total_bytes = 8;
    const auto id = database.insert_download(record);
    REQUIRE(id);

    vortex::OffsetFileWriter writer;
    REQUIRE(writer.open(path, 8).has_value());
    vortex::SegmentCheckpointManager manager(writer, database, path);
    const std::array bytes{std::byte{'a'}, std::byte{'b'}, std::byte{'c'}};
    const std::string sha = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    REQUIRE(manager.commit_segment(*id, 0, 0, bytes, sha, 10).has_value());
    const auto checkpoints = database.list_segment_checkpoints(*id);
    REQUIRE(checkpoints);
    REQUIRE(checkpoints->size() == 1);
    CHECK(checkpoints->front().checksum_sha256 == sha);
    REQUIRE(writer.close().has_value());
    std::filesystem::remove(path);
}

TEST_CASE("Segment checkpoint rejects a digest mismatch without trusting written bytes") {
    const auto path = unique_path();
    TemporaryDatabase temporary;
    vortex::SqliteDatabase database;
    REQUIRE(database.open(temporary.path()).has_value());
    REQUIRE(database.migrate().has_value());
    vortex::DownloadRecord record;
    record.url = "https://example.test/mismatch.bin";
    record.destination = path.string();
    record.total_bytes = 3;
    const auto id = database.insert_download(record);
    REQUIRE(id);
    vortex::OffsetFileWriter writer;
    REQUIRE(writer.open(path, 3).has_value());
    vortex::SegmentCheckpointManager manager(writer, database, path);
    const std::array bytes{std::byte{'b'}, std::byte{'a'}, std::byte{'d'}};
    CHECK_FALSE(manager.commit_segment(*id, 0, 0, bytes, std::string(64, '0'), 11).has_value());
    const auto checkpoints = database.list_segment_checkpoints(*id);
    REQUIRE(checkpoints);
    CHECK(checkpoints->empty());
    REQUIRE(writer.close().has_value());
    std::filesystem::remove(path);
}

TEST_CASE("Segment checkpoint recovery prunes only malformed or modified segment digests") {
    const auto path = unique_path();
    TemporaryDatabase temporary;
    vortex::SqliteDatabase database;
    REQUIRE(database.open(temporary.path()).has_value());
    REQUIRE(database.migrate().has_value());
    vortex::DownloadRecord record;
    record.url = "https://example.test/prune.bin";
    record.destination = path.string();
    record.total_bytes = 8;
    const auto id = database.insert_download(record);
    REQUIRE(id);
    vortex::OffsetFileWriter writer;
    REQUIRE(writer.open(path, 8).has_value());
    const std::array first{std::byte{'a'}, std::byte{'b'}, std::byte{'c'}};
    const std::array second{std::byte{'d'}, std::byte{'e'}, std::byte{'f'}};
    REQUIRE(writer.write_at(0, first).has_value());
    REQUIRE(writer.write_at(4, second).has_value());
    REQUIRE(writer.flush_durable().has_value());
    const std::string good = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    REQUIRE(database.record_segment_checkpoint(vortex::SegmentCheckpoint{
        *id, 0, 0, 3, good, 1
    }).has_value());
    REQUIRE(database.record_segment_checkpoint(vortex::SegmentCheckpoint{
        *id, 1, 4, 3, std::string(64, '0'), 1
    }).has_value());
    REQUIRE(database.record_segment_checkpoint(vortex::SegmentCheckpoint{
        *id, 2, 7, 1, "", 1
    }).has_value());

    vortex::SegmentCheckpointManager manager(writer, database, path);
    const auto removed = manager.verify_and_prune(*id);
    REQUIRE(removed);
    CHECK(*removed == 2);
    const auto remaining = database.list_segment_checkpoints(*id);
    REQUIRE(remaining);
    REQUIRE(remaining->size() == 1);
    CHECK(remaining->front().segment_index == 0);
    REQUIRE(writer.close().has_value());
    std::filesystem::remove(path);
}

TEST_CASE("Segment checkpoint recovery preserves metadata when the partial file is unavailable") {
    const auto path = unique_path();
    TemporaryDatabase temporary;
    vortex::SqliteDatabase database;
    REQUIRE(database.open(temporary.path()).has_value());
    REQUIRE(database.migrate().has_value());
    vortex::DownloadRecord record;
    record.url = "https://example.test/missing.bin";
    record.destination = path.string();
    record.total_bytes = 3;
    const auto id = database.insert_download(record);
    REQUIRE(id);
    REQUIRE(database.record_segment_checkpoint(vortex::SegmentCheckpoint{
        *id, 0, 0, 3,
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", 1
    }).has_value());
    vortex::OffsetFileWriter writer;
    vortex::SegmentCheckpointManager manager(writer, database, path);
    CHECK_FALSE(manager.verify_and_prune(*id).has_value());
    const auto remaining = database.list_segment_checkpoints(*id);
    REQUIRE(remaining);
    CHECK(remaining->size() == 1);
}
