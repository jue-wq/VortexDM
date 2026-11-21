#include "vortex/crash_safe_file_writer.hpp"
#include "vortex/chunk_assembler.hpp"
#include "native_messaging_host.hpp"
#include "vortex/sqlite_repository.hpp"
#include "vortex/segment_scheduler.hpp"
#include "vortex/task.hpp"
#include "vortex/process_runner.hpp"
#include "vortex/url.hpp"

#include <openssl/sha.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <span>
#include <thread>
#include <vector>

namespace {
std::string encode_base64(std::string_view input) {
    static constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string encoded;
    encoded.reserve(((input.size() + 2U) / 3U) * 4U);
    for (std::size_t offset = 0; offset < input.size(); offset += 3U) {
        const auto a = static_cast<unsigned char>(input[offset]);
        const auto b = offset + 1U < input.size() ? static_cast<unsigned char>(input[offset + 1U]) : 0U;
        const auto c = offset + 2U < input.size() ? static_cast<unsigned char>(input[offset + 2U]) : 0U;
        const unsigned int bits = (static_cast<unsigned int>(a) << 16U) |
            (static_cast<unsigned int>(b) << 8U) | static_cast<unsigned int>(c);
        encoded.push_back(alphabet[(bits >> 18U) & 0x3fU]);
        encoded.push_back(alphabet[(bits >> 12U) & 0x3fU]);
        encoded.push_back(offset + 1U < input.size() ? alphabet[(bits >> 6U) & 0x3fU] : '=');
        encoded.push_back(offset + 2U < input.size() ? alphabet[bits & 0x3fU] : '=');
    }
    return encoded;
}

void append_frame(std::string& stream, const nlohmann::json& message) {
    const std::string payload = message.dump();
    const auto size = static_cast<std::uint32_t>(payload.size());
    stream.push_back(static_cast<char>(size & 0xffU));
    stream.push_back(static_cast<char>((size >> 8U) & 0xffU));
    stream.push_back(static_cast<char>((size >> 16U) & 0xffU));
    stream.push_back(static_cast<char>((size >> 24U) & 0xffU));
    stream.append(payload);
}

std::vector<nlohmann::json> read_frames(const std::string& stream) {
    std::vector<nlohmann::json> messages;
    std::size_t offset = 0;
    while (offset + 4U <= stream.size()) {
        const auto b0 = static_cast<std::uint32_t>(static_cast<unsigned char>(stream[offset]));
        const auto b1 = static_cast<std::uint32_t>(static_cast<unsigned char>(stream[offset + 1U]));
        const auto b2 = static_cast<std::uint32_t>(static_cast<unsigned char>(stream[offset + 2U]));
        const auto b3 = static_cast<std::uint32_t>(static_cast<unsigned char>(stream[offset + 3U]));
        const std::uint32_t length = b0 | (b1 << 8U) | (b2 << 16U) | (b3 << 24U);
        offset += 4U;
        if (length == 0U || static_cast<std::size_t>(length) > stream.size() - offset) return {};
        messages.push_back(nlohmann::json::parse(stream.substr(offset, length)));
        offset += length;
    }
    if (offset != stream.size()) return {};
    return messages;
}

bool test_process_runner() {
#ifdef _WIN32
    const auto executable = std::filesystem::path("C:/Windows/System32/whoami.exe");
    const auto result = vortex::run_process(executable, {});
    return result && result->exit_code == 0 && !result->output.empty();
#else
    vortex::ProcessOptions limited;
    limited.max_output_bytes = 4U;
    const auto output = vortex::run_process("/bin/echo", {"safe; not a shell command"}, limited);
    if (!output || output->exit_code != 0 || !output->output_truncated || output->output.size() != 4U) return false;
    vortex::ProcessOptions timeout;
    timeout.timeout = std::chrono::milliseconds(50);
    const auto timed = vortex::run_process("/bin/sleep", {"2"}, timeout);
    return !timed && timed.error().code == "process_timeout";
#endif
}

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

    vortex::CrashSafeFileWriter recovered_writer(path);
    if (!recovered_writer.open_existing(total_size)) return false;
    const std::span<const std::byte> first_byte(expected.data(), 1U);
    if (!recovered_writer.write(0U, first_byte) || !recovered_writer.flush()) return false;
    recovered_writer.close();

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


bool test_segment_scheduler() {
    const auto ranges = vortex::make_byte_ranges(1000U, 4U, 100U);
    if (!ranges || ranges->size() != 4U) return false;
    std::uint64_t total = 0;
    for (const auto& range : *ranges) total += range.size();
    if (total != 1000U || ranges->front().start != 0U || ranges->back().end != 999U) return false;

    const auto empty = vortex::make_byte_ranges(0U, 4U, 100U);
    if (!empty || !empty->empty()) return false;
    const auto single = vortex::make_byte_ranges(1U, 8U, 1U);
    if (!single || single->size() != 1U || single->front().start != 0U || single->front().end != 0U) return false;
    if (vortex::make_byte_ranges(1000U, 0U, 100U)) return false;
    if (vortex::make_byte_ranges(1000U, 33U, 100U)) return false;
    if (vortex::make_byte_ranges(1000U, 4U, 0U)) return false;

    const auto huge = vortex::make_byte_ranges(UINT64_MAX, 4U, UINT64_MAX - 1U);
    if (!huge || huge->empty()) return false;
    std::uint64_t huge_total = 0;
    for (const auto& range : *huge) {
        if (range.end < range.start) return false;
        huge_total += range.size();
    }
    if (huge_total != UINT64_MAX) return false;

    vortex::ByteRange almost_max{0U, 0U, UINT64_MAX};
    if (vortex::steal_tail(almost_max, UINT64_MAX / 2U + 1U)) return false;

    vortex::ByteRange active{0U, 0U, 999U};
    const auto stolen = vortex::steal_tail(active, 100U);
    if (!stolen || active.end + 1U != stolen->start) return false;
    return active.size() + stolen->size() == 1000U;
}

bool test_chunk_assembler_round_trip() {
    const std::string payload(2U * 1024U * 1024U + 173U, 'v');
    constexpr std::size_t chunk_size = 256U * 1024U;
    const std::size_t total = (payload.size() + chunk_size - 1U) / chunk_size;
    vortex::ChunkAssembler assembler;
    std::string assembled;
    for (std::size_t reverse = total; reverse > 0U; --reverse) {
        const std::size_t index = reverse - 1U;
        const std::size_t offset = index * chunk_size;
        const std::string_view part(payload.data() + offset, std::min(chunk_size, payload.size() - offset));
        const auto result = assembler.add("test-batch", index, total, encode_base64(part));
        if (!result) return false;
        if (result->complete) assembled = result->payload;
    }
    if (assembled != payload || assembler.pending_count() != 0U) return false;

    auto first = assembler.add("duplicate", 0U, 2U, encode_base64("first"));
    if (!first || first->complete) return false;
    const auto duplicate = assembler.add("duplicate", 0U, 2U, encode_base64("again"));
    return !duplicate;
}

bool test_native_host_batch_chunk_integration() {
    nlohmann::json batch = {
        {"items", nlohmann::json::array({{{"url","https://example.com/large.bin"},{"padding",std::string(2U * 1024U * 1024U, 'x')}}})},
        {"source_page","https://example.com"}
    };
    const std::string serialized = batch.dump();
    constexpr std::size_t chunk_size = 256U * 1024U;
    const std::size_t total = (serialized.size() + chunk_size - 1U) / chunk_size;
    std::string input_bytes;
    for (std::size_t index = total; index > 0U; --index) {
        const std::size_t chunk_index = index - 1U;
        const std::size_t offset = chunk_index * chunk_size;
        const std::string_view part(serialized.data() + offset, std::min(chunk_size, serialized.size() - offset));
        const nlohmann::json request = {
            {"protocol_version",1},{"type","add_batch_chunk"},
            {"request_id","chunk-" + std::to_string(chunk_index)},{"timestamp",1},
            {"payload",{{"chunk_id","batch-e2e"},{"chunk_index",chunk_index},{"chunk_total",total},{"chunk_data",encode_base64(part)}}}
        };
        append_frame(input_bytes, request);
    }

    std::istringstream input(input_bytes);
    std::ostringstream output;
    vortex::NativeMessagingHost host;
    if (host.run(input, output) != 0) return false;
    const auto responses = read_frames(output.str());
    if (responses.size() != total) return false;
    for (std::size_t index = 0; index < responses.size(); ++index) {
        const auto& response = responses[index];
        if (response.value("type", std::string{}) != "ack" ||
            response["payload"].value("accepted", false) != true) return false;
    }
    const auto& last = responses.back();
    return last["payload"].value("complete", false) &&
        last["payload"].value("count", std::size_t{0}) == 1U;
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
    const auto journal = repository.list_segments(stream.id);
    if (!journal || journal->size() != 1U || journal->front().downloaded_bytes != 512U ||
        journal->front().status != "Downloading") return false;

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
    if (!test_process_runner()) {
        std::cerr << "Shell-free process runner test failed\\n";
        return 7;
    }
    if (!test_url_and_state_machine()) {
        std::cerr << "URL or task state-machine test failed\n";
        return 1;
    }
    if (!test_crash_safe_concurrent_writes()) {
        std::cerr << "16-thread crash-safe writer SHA-256 test failed\n";
        return 2;
    }
    if (!test_segment_scheduler()) {
        std::cerr << "Segment scheduler test failed\n";
        return 3;
    }
    if (!test_chunk_assembler_round_trip()) {
        std::cerr << "C++ chunk assembler test failed\n";
        return 4;
    }
    if (!test_native_host_batch_chunk_integration()) {
        std::cerr << "Native Messaging host batch chunk integration test failed\n";
        return 5;
    }
    if (!test_sqlite_migrations_and_segment_journal()) {
        std::cerr << "SQLite migration / segment journal test failed\n";
        return 6;
    }
    std::cout << "core tests passed: URL/state, 100 MiB concurrent write SHA-256, segment scheduler, 2 MiB Native Messaging batch, SQLite migration and segment journal\n";
    return 0;
}
