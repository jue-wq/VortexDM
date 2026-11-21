#include "core/download/range_download_coordinator.hpp"\n#include "core/storage/sqlite_database.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

std::filesystem::path temporary_path() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
        ("vortex-range-transfer-" + std::to_string(stamp) + ".part");
}

} // namespace

TEST_CASE("Range coordinator validates each response before committing bytes") {
    const auto path = temporary_path();
    constexpr std::uint64_t total = 10;
    vortex::OffsetFileWriter writer;
    REQUIRE(writer.open(path, total));

    const std::array<std::byte, 10> payload{
        std::byte{'0'}, std::byte{'1'}, std::byte{'2'}, std::byte{'3'}, std::byte{'4'},
        std::byte{'5'}, std::byte{'6'}, std::byte{'7'}, std::byte{'8'}, std::byte{'9'}
    };
    std::size_t calls = 0;
    const vortex::RangeRequestExecutor executor =
        [&payload, &calls](const vortex::HttpRequest& request,
                           const vortex::HttpBodySink& sink, std::stop_token) {
            REQUIRE(request.range.has_value());
            const auto range = *request.range;
            CHECK(range.first == calls * 4U);
            const auto count = static_cast<std::size_t>(range.size());
            REQUIRE(sink(std::span<const std::byte>{
                payload.data() + static_cast<std::ptrdiff_t>(range.first), count}));
            vortex::HttpResponse response;
            response.status_code = 206;
            response.headers["content-range"] = "bytes " +
                std::to_string(range.first) + "-" + std::to_string(range.last) + "/10";
            response.headers["content-length"] = std::to_string(count);
            response.bytes_received = count;
            ++calls;
            return vortex::Result<vortex::HttpResponse>{std::move(response)};
        };

    REQUIRE(vortex::RangeDownloadCoordinator::transfer(
        "https://example.test/file", total, 4, writer, executor));
    CHECK(calls == 3);
    REQUIRE(writer.close());

    std::ifstream input(path, std::ios::binary);
    REQUIRE(input.good());
    std::array<char, 10> actual{};
    input.read(actual.data(), static_cast<std::streamsize>(actual.size()));
    CHECK(std::string(actual.data(), actual.size()) == "0123456789");
    input.close();
    std::filesystem::remove(path);
}

TEST_CASE("Range coordinator never writes a response with an invalid Content-Range") {
    const auto path = temporary_path();
    vortex::OffsetFileWriter writer;
    REQUIRE(writer.open(path, 4));
    const vortex::RangeRequestExecutor executor =
        [](const vortex::HttpRequest&, const vortex::HttpBodySink& sink, std::stop_token) {
            const std::array<std::byte, 4> body{
                std::byte{'b'}, std::byte{'a'}, std::byte{'d'}, std::byte{'!'}};
            REQUIRE(sink(body));
            vortex::HttpResponse response;
            response.status_code = 206;
            response.headers["content-range"] = "bytes 1-4/4";
            response.headers["content-length"] = "4";
            response.bytes_received = 4;
            return vortex::Result<vortex::HttpResponse>{std::move(response)};
        };

    const auto result = vortex::RangeDownloadCoordinator::transfer(
        "https://example.test/file", 4, 4, writer, executor);
    CHECK_FALSE(result.has_value());
    REQUIRE(writer.close());

    std::ifstream input(path, std::ios::binary);
    REQUIRE(input.good());
    std::array<char, 4> actual{};
    input.read(actual.data(), static_cast<std::streamsize>(actual.size()));
    CHECK(std::string(actual.data(), actual.size()) == std::string(4, '\0'));
    input.close();
    std::filesystem::remove(path);
}

TEST_CASE("Range coordinator retries transient HTTP responses without writing bad data") {
    const auto path = temporary_path();
    vortex::OffsetFileWriter writer;
    REQUIRE(writer.open(path, 3));
    std::size_t attempts = 0;
    std::vector<std::chrono::milliseconds> delays;
    const vortex::RangeRequestExecutor executor =
        [&attempts](const vortex::HttpRequest&, const vortex::HttpBodySink& sink,
                    std::stop_token) {
            ++attempts;
            vortex::HttpResponse response;
            if (attempts == 1) {
                response.status_code = 503;
                response.headers["retry-after"] = "2";
                return vortex::Result<vortex::HttpResponse>{std::move(response)};
            }
            const std::array<std::byte, 3> body{
                std::byte{'o'}, std::byte{'k'}, std::byte{'!'}};
            REQUIRE(sink(body));
            response.status_code = 206;
            response.headers["content-range"] = "bytes 0-2/3";
            response.headers["content-length"] = "3";
            response.bytes_received = 3;
            return vortex::Result<vortex::HttpResponse>{std::move(response)};
        };
    vortex::RangeTransferOptions options;
    options.wait_before_retry = [&delays](std::chrono::milliseconds delay, std::stop_token) {
        delays.push_back(delay);
        return true;
    };

    REQUIRE(vortex::RangeDownloadCoordinator::transfer(
        "https://example.test/file", 3, 3, writer, executor, options));
    CHECK(attempts == 2);
    REQUIRE(delays.size() == 1);
    CHECK(delays.front() == std::chrono::seconds{2});
    REQUIRE(writer.close());
    std::filesystem::remove(path);
}

TEST_CASE("Range coordinator retries transient transport errors") {
    const auto path = temporary_path();
    vortex::OffsetFileWriter writer;
    REQUIRE(writer.open(path, 2));
    std::size_t attempts = 0;
    const vortex::RangeRequestExecutor executor =
        [&attempts](const vortex::HttpRequest&, const vortex::HttpBodySink& sink,
                    std::stop_token) {
            ++attempts;
            if (attempts == 1) return vortex::Result<vortex::HttpResponse>{
                std::unexpected(vortex::VortexError::make(
                    vortex::ErrorCode::network_error, "temporary failure"))};
            const std::array<std::byte, 2> body{std::byte{'o'}, std::byte{'k'}};
            REQUIRE(sink(body));
            vortex::HttpResponse response;
            response.status_code = 206;
            response.headers["content-range"] = "bytes 0-1/2";
            response.headers["content-length"] = "2";
            response.bytes_received = 2;
            return vortex::Result<vortex::HttpResponse>{std::move(response)};
        };
    vortex::RangeTransferOptions options;
    options.wait_before_retry = [](std::chrono::milliseconds, std::stop_token) { return true; };

    REQUIRE(vortex::RangeDownloadCoordinator::transfer(
        "https://example.test/file", 2, 2, writer, executor, options));
    CHECK(attempts == 2);
    REQUIRE(writer.close());
    std::filesystem::remove(path);
}

TEST_CASE("Range coordinator durably checkpoints each validated segment") {
    const auto path = temporary_path();
    const auto database_path = path.string() + ".sqlite";
    vortex::SqliteDatabase database;
    REQUIRE(database.open(database_path).has_value());
    REQUIRE(database.migrate().has_value());
    vortex::DownloadRecord record;
    record.url = "https://example.test/checkpointed.bin";
    record.destination = path.string();
    record.total_bytes = 6;
    const auto id = database.insert_download(record);
    REQUIRE(id);
    vortex::OffsetFileWriter writer;
    REQUIRE(writer.open(path, 6).has_value());
    vortex::SegmentCheckpointManager manager(writer, database, path);
    const std::array<std::byte, 6> payload{
        std::byte{'a'}, std::byte{'b'}, std::byte{'c'},
        std::byte{'d'}, std::byte{'e'}, std::byte{'f'}
    };
    const vortex::RangeRequestExecutor executor =
        [&payload](const vortex::HttpRequest& request, const vortex::HttpBodySink& sink,
                   std::stop_token) {
            const auto range = *request.range;
            const auto count = static_cast<std::size_t>(range.size());
            REQUIRE(sink(std::span<const std::byte>{
                payload.data() + static_cast<std::ptrdiff_t>(range.first), count}));
            vortex::HttpResponse response;
            response.status_code = 206;
            response.headers["content-range"] = "bytes " +
                std::to_string(range.first) + "-" + std::to_string(range.last) + "/6";
            response.headers["content-length"] = std::to_string(count);
            response.bytes_received = count;
            return vortex::Result<vortex::HttpResponse>{std::move(response)};
        };
    REQUIRE(vortex::RangeDownloadCoordinator::transfer(
        "https://example.test/checkpointed.bin", 6, 3, writer, executor, {}, {},
        &manager, *id, 42).has_value());
    const auto checkpoints = database.list_segment_checkpoints(*id);
    REQUIRE(checkpoints);
    REQUIRE(checkpoints->size() == 2);
    CHECK(checkpoints->at(0).offset == 0);
    CHECK(checkpoints->at(1).offset == 3);
    const auto pruned = manager.verify_and_prune(*id);
    REQUIRE(pruned);
    CHECK(*pruned == 0);
    REQUIRE(writer.close());
    REQUIRE(database.close().has_value());
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    std::filesystem::remove(database_path, ignored);
    std::filesystem::remove(database_path + "-wal", ignored);
    std::filesystem::remove(database_path + "-shm", ignored);
}
