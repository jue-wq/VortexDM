#include "vortex/crash_safe_file_writer.hpp"
#include "vortex/sqlite_repository.hpp"
#include "vortex/task.hpp"
#include "vortex/url.hpp"

#include <openssl/sha.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <thread>
#include <vector>

namespace {
bool test_url_and_state_machine() {
    const auto valid = vortex::parse_http_url("https://example.com/video.mp4");
    if (!valid || valid->host != "example.com") return false;
    if (vortex::parse_http_url("file:///etc/passwd")) return false;
    if (vortex::parse_http_url("javascript:alert(1)")) return false;
    if (vortex::parse_http_url("https://example.com/a b")) return false;
    if (!vortex::can_transition(vortex::TaskState::Added, vortex::TaskState::Queued)) return false;
    return !vortex::can_transition(vortex::TaskState::Completed, vortex::TaskState::Downloading);
}

bool test_crash_safe_concurrent_writes() {
    constexpr std::size_t segment_size = 1024U * 1024U;
    constexpr std::size_t segment_count = 100U;
    constexpr std::size_t thread_count = 16U;
    constexpr std::size_t total_size = segment_size * segment_count;
    const auto path = std::filesystem::temp_directory_path() / "vortexdm-crash-safe-test.bin";
    std::error_code ignored;
    std::filesystem::remove(path, ignored);

    std::vector<std::byte> expected(total_size);
    for (std::size_t index = 0; index < expected.size(); ++index) {
        const auto value = static_cast<unsigned int>((index * 29U + index / 97U) & 0xffU);
        expected[index] = static_cast<std::byte>(value);
    }
    vortex::CrashSafeFileWriter writer(path);
    if (!writer.open(total_size)) return false;

    std::atomic<bool> write_ok{true};
    std::vector<std::thread> workers;
    workers.reserve(thread_count);
    for (std::size_t thread_index = 0; thread_index < thread_count; ++thread_index) {
        workers.emplace_back([thread_index, &writer, &expected, &write_ok] {
            for (std::size_t order = thread_index; order < segment_count; order += thread_count) {
                const std::size_t segment_index = (order * 37U) % segment_count;
                const std::size_t offset = segment_index * segment_size;
                const std::span<const std::byte> block(expected.data() + offset, segment_size);
                if (!writer.write(static_cast<std::uint64_t>(offset), block)) {
                    write_ok.store(false, std::memory_order_relaxed);
                    return;
                }
            }
        });
    }
    for (auto& worker : workers) worker.join();
    if (!write_ok.load(std::memory_order_relaxed) || !writer.flush()) return false;
    writer.close();

    std::vector<std::byte> actual(total_size);
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    input.read(reinterpret_cast<char*>(actual.data()), static_cast<std::streamsize>(actual.size()));
    if (input.gcount() != static_cast<std::streamsize>(actual.size())) return false;

    std::array<unsigned char, SHA256_DIGEST_LENGTH> expected_hash{};
    std::array<unsigned char, SHA256_DIGEST_LENGTH> actual_hash{};
    (void)SHA256(reinterpret_cast<const unsigned char*>(expected.data()), expected.size(), expected_hash.data());
    (void)SHA256(reinterpret_cast<const unsigned char*>(actual.data()), actual.size(), actual_hash.data());
    std::filesystem::remove(path, ignored);
    return expected_hash == actual_hash;
}

bool test_sqlite_migrations_and_segment_journal() {
    const auto path = std::filesystem::temp_directory_path() / "vortexdm-repository-test.db";
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    std::filesystem::remove(path.string() + "-wal", ignored);
    std::filesystem::remove(path.string() + "-shm", ignored);

    vortex::SqliteRepository repository(path);
    if (!repository.initialize()) return false;
    const auto version = repository.schema_version();
    if (!version || *version != 1U) return false;

    vortex::DownloadRecord download;
    download.id = "download-test-1";
    download.url = "https://example.com/video.bin";
    download.filename = "video.bin";
    download.save_dir = "test-output";
    download.status = "Downloading";
    download.total_bytes = 2048U;
    download.downloaded_bytes = 512U;
    if (!repository.save_download(download)) return false;

    vortex::StreamRecord stream;
    stream.id = "stream-test-1";
    stream.download_id = download.id;
    stream.url = download.url;
    stream.total_bytes = download.total_bytes;
    stream.downloaded_bytes = download.downloaded_bytes;
    if (!repository.save_stream(stream)) return false;

    vortex::SegmentRecord segment;
    segment.stream_id = stream.id;
    segment.segment_index = 0U;
    segment.start_offset = 0U;
    segment.end_offset = 1023U;
    segment.downloaded_bytes = 512U;
    segment.status = "Downloading";
    if (!repository.save_segment_checkpoint(segment)) return false;

    const auto records = repository.list_downloads();
    if (!records || records->size() != 1U) return false;
    const bool matched = records->front().id == download.id &&
        records->front().downloaded_bytes == 512U &&
        records->front().status == "Downloading";
    std::filesystem::remove(path, ignored);
    std::filesystem::remove(path.string() + "-wal", ignored);
    std::filesystem::remove(path.string() + "-shm", ignored);
    return matched;
}
}

int main() {
    if (!test_url_and_state_machine()) {
        std::cerr << "URL or task state-machine test failed\n";
        return 1;
    }
    if (!test_crash_safe_concurrent_writes()) {
        std::cerr << "16-thread crash-safe writer SHA-256 test failed\n";
        return 2;
    }
    if (!test_sqlite_migrations_and_segment_journal()) {
        std::cerr << "SQLite migration / segment journal test failed\n";
        return 3;
    }
    std::cout << "core tests passed: URL/state, 100 MiB concurrent write SHA-256, SQLite migration and segment journal\n";
    return 0;
}
