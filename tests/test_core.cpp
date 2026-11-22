#include "vortex/crash_safe_file_writer.hpp"
#include "vortex/chunk_assembler.hpp"
#include "native_messaging_host.hpp"
#include "vortex/sqlite_repository.hpp"
#include "vortex/segment_scheduler.hpp"
#include "vortex/task.hpp"
#include "vortex/process_runner.hpp"
#include "vortex/ytdlp_extractor.hpp"
#include "vortex/segment_checkpoint.hpp"
#include "vortex/segment_recovery.hpp"
#include "vortex/ytdlp_toolchain.hpp"
#include "vortex/stream_manifest.hpp"
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

bool test_hls_and_dash_manifests() {
    const std::string master = "#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=1280000,CODECS=\"avc1.4d401f,mp4a.40.2\",RESOLUTION=1280x720\n720/index.m3u8\n";
    const auto parsed_master = vortex::parse_hls_playlist(master, "https://cdn.example/master.m3u8");
    if (!parsed_master || !parsed_master->master_playlist || parsed_master->variants.size()!=1U) return false;
    if (parsed_master->variants.front().uri != "https://cdn.example/720/index.m3u8") return false;

    const std::string media = "#EXTM3U\n#EXT-X-KEY:METHOD=AES-128,URI=\"keys/key.bin\"\n#EXT-X-MAP:URI=\"init.mp4\"\n#EXTINF:4.5,first\nseg-1.m4s\n#EXT-X-ENDLIST\n";
    const auto parsed_media = vortex::parse_hls_playlist(media, "https://cdn.example/path/index.m3u8");
    if (!parsed_media || parsed_media->live || !parsed_media->aes128_key_uri ||
        *parsed_media->aes128_key_uri != "https://cdn.example/path/keys/key.bin" ||
        !parsed_media->initialization_uri || parsed_media->segments.size()!=1U ||
        parsed_media->segments.front().duration_seconds != 4.5) return false;
    if (vortex::parse_hls_playlist("#EXTM3U\n#EXT-X-KEY:METHOD=SAMPLE-AES,URI=\"key\"\n", "https://cdn.example/a.m3u8")) return false;

    const std::string mpd = R"XML(<?xml version="1.0"?>
<MPD xmlns="urn:mpeg:dash:schema:mpd:2011" type="static">
 <Period>
  <AdaptationSet mimeType="video/mp4" codecs="avc1">
   <Representation id="template" bandwidth="1000000" width="1280" height="720">
    <BaseURL>https://cdn.example/template/</BaseURL>
    <SegmentTemplate timescale="1000" duration="4000" startNumber="5" media="chunk-$Number$.m4s" initialization="init.mp4"><SegmentTimeline><S t="0" d="4000" r="2"/></SegmentTimeline></SegmentTemplate>
   </Representation>
  </AdaptationSet>
  <AdaptationSet mimeType="audio/mp4">
   <Representation id="list" bandwidth="128000">
    <BaseURL>https://cdn.example/list/</BaseURL>
    <SegmentList><Initialization sourceURL="init-audio.mp4"/><SegmentURL media="a1.m4s"/><SegmentURL media="a2.m4s"/></SegmentList>
   </Representation>
  </AdaptationSet>
  <AdaptationSet mimeType="video/mp4">
   <Representation id="base" bandwidth="500000">
    <BaseURL>https://cdn.example/base/file.mp4</BaseURL>
    <SegmentBase indexRange="100-200"><Initialization sourceURL="init.bin" range="0-99"/></SegmentBase>
   </Representation>
  </AdaptationSet>
 </Period>
</MPD>)XML";
    const auto parsed_dash = vortex::parse_dash_mpd(mpd, "https://cdn.example/manifest.mpd");
    if (!parsed_dash || parsed_dash->dynamic || parsed_dash->representations.size()!=3U) return false;
    const auto& templ = parsed_dash->representations[0];
    if (!templ.segment_template || templ.segment_template->start_number!=5U ||
        templ.segment_template->duration!=4000U || templ.segment_template->timeline.size()!=1U ||
        templ.segment_template->timeline.front().repeat!=2 || templ.id!="template") return false;
    const auto& list = parsed_dash->representations[1];
    if (list.segment_urls.size()!=2U || !list.initialization_url ||
        *list.initialization_url!="https://cdn.example/list/init-audio.mp4") return false;
    const auto& base_rep = parsed_dash->representations[2];
    if (base_rep.segment_base_index_range!="100-200" || !base_rep.segment_base_initialization_url ||
        *base_rep.segment_base_initialization_url!="https://cdn.example/base/init.bin" ||
        base_rep.segment_base_initialization_range!="0-99") return false;
    return !vortex::parse_dash_mpd("<not-mpd>", "https://cdn.example/manifest.mpd");
}

bool test_ytdlp_toolchain_verification_and_rollback() {
    const auto directory = std::filesystem::temp_directory_path() / "vortexdm-toolchain-test";
    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
    std::filesystem::create_directories(directory, ignored);
    if (ignored) return false;
    const auto candidate = directory / "candidate";
    const auto active = directory / "yt-dlp";
    {
        std::ofstream output(candidate, std::ios::binary);
        output << "verified-candidate";
    }
    {
        std::ofstream output(active, std::ios::binary);
        output << "previous-version";
    }
    const std::string candidate_bytes = "verified-candidate";
    std::array<unsigned char, SHA256_DIGEST_LENGTH> digest{};
    (void)SHA256(reinterpret_cast<const unsigned char*>(candidate_bytes.data()), candidate_bytes.size(), digest.data());
    static constexpr char hex[] = "0123456789abcdef";
    std::string expected;
    for (const auto value : digest) { expected.push_back(hex[value >> 4U]); expected.push_back(hex[value & 0x0fU]); }
    if (!vortex::verify_sha256_file(candidate, expected)) return false;
    if (vortex::verify_sha256_file(candidate, std::string(64U, '0'))) return false;
    if (!vortex::activate_verified_tool(candidate, active, expected)) return false;
    {
        std::ifstream input(active, std::ios::binary);
        std::string actual((std::istreambuf_iterator<char>(input)), {});
        if (actual != candidate_bytes) return false;
    }
    if (!vortex::rollback_tool(active)) return false;
    {
        std::ifstream input(active, std::ios::binary);
        std::string actual((std::istreambuf_iterator<char>(input)), {});
        if (actual != "previous-version") return false;
    }
    std::filesystem::remove_all(directory, ignored);
    return true;
}

bool test_ytdlp_metadata_parser() {
    const std::string payload = R"JSON({
      "id":"playlist-1","title":"Sample playlist","webpage_url":"https://example.com/list",
      "entries":[
        {"id":"video-1","title":"First video","webpage_url":"https://example.com/v1","duration":12.5,
         "formats":[{"format_id":"18","ext":"mp4","resolution":"640x360","vcodec":"avc1","acodec":"mp4a","protocol":"https","filesize":1234}]},
        {"id":"video-2","title":"Second video","webpage_url":"https://example.com/v2"}
      ]
    })JSON";
    const auto parsed = vortex::parse_ytdlp_metadata_json(payload);
    if (!parsed || !parsed->playlist || parsed->entries.size() != 2U) return false;
    const auto& first = parsed->entries.front();
    if (first.id != "video-1" || first.formats.size() != 1U) return false;
    if (first.formats.front().file_size != 1234U || first.formats.front().extension != "mp4") return false;
    if (vortex::parse_ytdlp_metadata_json("not-json")) return false;
    if (vortex::parse_ytdlp_metadata_json("[]")) return false;
    return !vortex::extract_ytdlp_metadata("/usr/bin/yt-dlp", "file:///etc/passwd");
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

    const auto recovery = vortex::plan_recovery_ranges(300U, {
        {0U, 99U, 100U}, {100U, 199U, 50U}
    });
    if (!recovery || recovery->size() != 2U) return false;
    if ((*recovery)[0].start != 150U || (*recovery)[0].end != 199U ||
        (*recovery)[1].start != 200U || (*recovery)[1].end != 299U) return false;
    if (vortex::plan_recovery_ranges(100U, {{0U, 60U, 10U}, {50U, 99U, 50U}})) return false;
    if (vortex::plan_recovery_ranges(100U, {{0U, 99U, 101U}})) return false;

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
        {"source_page","https://example.com"},
        {"queue_id","queue-test"}
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
    const auto database_path = std::filesystem::temp_directory_path() / "vortexdm-native-host-test.sqlite3";
    std::error_code database_ignored;
    std::filesystem::remove(database_path, database_ignored);
    std::filesystem::remove(database_path.string()+"-wal", database_ignored);
    std::filesystem::remove(database_path.string()+"-shm", database_ignored);
    int host_status = 0;
    {
        vortex::NativeMessagingHost host(database_path);
        host_status = host.run(input, output);
    }
    if (host_status != 0) return false;
    const auto responses = read_frames(output.str());
    if (responses.size() != total) return false;
    for (std::size_t index = 0; index < responses.size(); ++index) {
        const auto& response = responses[index];
        if (response.value("type", std::string{}) != "ack" ||
            response["payload"].value("accepted", false) != true) return false;
    }
    const auto& last = responses.back();
    bool accepted = last["payload"].value("complete", false) &&
        last["payload"].value("count", std::size_t{0}) == 1U;
    {
        vortex::SqliteRepository persisted(database_path);
        if (!persisted.initialize()) accepted = false;
        const auto queued = persisted.list_downloads();
        if (!queued || queued->size()!=1U || queued->front().queue_id!="queue-test" ||
            queued->front().status!="Queued") accepted = false;
    }
    std::filesystem::remove(database_path, database_ignored);
    std::filesystem::remove(database_path.string()+"-wal", database_ignored);
    std::filesystem::remove(database_path.string()+"-shm", database_ignored);
    return accepted;
}

bool test_native_host_task_control() {
    const auto database_path = std::filesystem::temp_directory_path() / "vortexdm-task-control-test.sqlite3";
    std::error_code ignored;
    std::filesystem::remove(database_path, ignored);
    std::filesystem::remove(database_path.string()+"-wal", ignored);
    std::filesystem::remove(database_path.string()+"-shm", ignored);
    vortex::SqliteRepository repository(database_path);
    if (!repository.initialize()) return false;
    vortex::DownloadRecord download;
    download.id = "task-control-test";
    download.url = "https://example.com/control.bin";
    download.filename = "control.bin";
    download.queue_id = "default";
    download.status = "Queued";
    if (!repository.save_download(download)) return false;

    const auto send_action = [&](const std::string& action, const std::string& request_id) {
        const nlohmann::json request = {
            {"protocol_version",1},{"type","task_control"},{"request_id",request_id},{"timestamp",1},
            {"payload",{{"task_id",download.id},{"action",action}}}
        };
        std::string bytes;
        append_frame(bytes, request);
        std::istringstream input(bytes);
        std::ostringstream output;
        vortex::NativeMessagingHost host(database_path);
        if (host.run(input, output) != 0) return false;
        const auto frames = read_frames(output.str());
        return frames.size() == 1U && frames.front().value("type",std::string{}) == "ack" &&
            frames.front()["payload"].value("accepted",false);
    };
    bool ok = send_action("pause","pause-1");
    auto status = repository.get_download_status(download.id);
    ok = ok && status && *status == "Paused";
    ok = ok && send_action("resume","resume-1");
    status = repository.get_download_status(download.id);
    ok = ok && status && *status == "Queued";
    ok = ok && send_action("cancel","cancel-1");
    status = repository.get_download_status(download.id);
    ok = ok && status && *status == "Cancelled";
    ok = ok && !send_action("resume","resume-cancelled");
    std::filesystem::remove(database_path, ignored);
    std::filesystem::remove(database_path.string()+"-wal", ignored);
    std::filesystem::remove(database_path.string()+"-shm", ignored);
    return ok;
}

bool test_segment_recovery_coordinator() {
    const auto db_path = std::filesystem::temp_directory_path() / "vortexdm-recovery-test.db";
    const auto partial_path = std::filesystem::temp_directory_path() / "vortexdm-recovery-test.part";
    std::error_code ignored;
    std::filesystem::remove(db_path, ignored);
    std::filesystem::remove(db_path.string() + "-wal", ignored);
    std::filesystem::remove(db_path.string() + "-shm", ignored);
    std::filesystem::remove(partial_path, ignored);

    vortex::SqliteRepository repository(db_path);
    if (!repository.initialize()) return false;
    vortex::DownloadRecord download;
    download.id = "recovery-download";
    download.url = "https://example.com/recovery.bin";
    download.filename = "recovery.bin";
    download.save_dir = "test";
    if (!repository.save_download(download)) return false;
    vortex::StreamRecord stream;
    stream.id = "recovery-stream";
    stream.download_id = download.id;
    stream.url = download.url;
    stream.temp_path = partial_path.string();
    stream.total_bytes = 100U;
    if (!repository.save_stream(stream)) return false;
    const auto stored_stream = repository.get_stream_for_download(stream.download_id);
    if (!stored_stream || !*stored_stream || (**stored_stream).id != stream.id ||
        (**stored_stream).temp_path != stream.temp_path) return false;

    {
        vortex::CrashSafeFileWriter initial(partial_path);
        if (!initial.open(100U)) return false;
        std::vector<std::byte> first(50U, std::byte{0x11});
        if (!initial.write(0U, first) || !initial.flush()) return false;
        vortex::SegmentRecord checkpoint;
        checkpoint.stream_id = stream.id;
        checkpoint.segment_index = 0U;
        checkpoint.start_offset = 0U;
        checkpoint.end_offset = 49U;
        checkpoint.downloaded_bytes = 50U;
        checkpoint.status = "Completed";
        if (!repository.save_segment_checkpoint(checkpoint)) return false;
    }

    const auto recovered = vortex::recover_download_segments(
        partial_path, 100U, stream.id, repository,
        [](vortex::CrashSafeFileWriter& writer, const vortex::ByteRange& range) -> vortex::Result<std::uint64_t> {
            std::vector<std::byte> bytes(static_cast<std::size_t>(range.size()), std::byte{0x22});
            auto written = writer.write(range.start, bytes);
            if (!written) return std::unexpected(written.error());
            return static_cast<std::uint64_t>(bytes.size());
        });
    if (!recovered) return false;

    std::vector<std::byte> actual(100U);
    std::ifstream input(partial_path, std::ios::binary);
    if (!input) return false;
    input.read(reinterpret_cast<char*>(actual.data()), static_cast<std::streamsize>(actual.size()));
    if (input.gcount() != static_cast<std::streamsize>(actual.size())) return false;
    if (!std::all_of(actual.begin(), actual.begin()+50, [](std::byte b){return b==std::byte{0x11};})) return false;
    if (!std::all_of(actual.begin()+50, actual.end(), [](std::byte b){return b==std::byte{0x22};})) return false;
    const auto journal = repository.list_segments(stream.id);
    if (!journal || journal->size()!=2U || journal->back().status!="Completed") return false;
    std::filesystem::remove(partial_path, ignored);
    std::filesystem::remove(db_path, ignored);
    std::filesystem::remove(db_path.string()+"-wal", ignored);
    std::filesystem::remove(db_path.string()+"-shm", ignored);
    return true;
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
    if (!version || *version != 2U) return false;

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
    const auto partial_path = std::filesystem::temp_directory_path() / "vortexdm-checkpoint-order.bin";
    std::error_code partial_ignored;
    std::filesystem::remove(partial_path, partial_ignored);
    vortex::CrashSafeFileWriter journal_writer(partial_path);
    if (!journal_writer.open(2048U)) return false;
    std::vector<std::byte> segment_bytes(512U, std::byte{0x2a});
    if (!journal_writer.write(0U, segment_bytes)) return false;
    if (!vortex::commit_segment_checkpoint(journal_writer, repository, segment)) return false;
    journal_writer.close();
    std::filesystem::remove(partial_path, partial_ignored);
    const auto journal = repository.list_segments(stream.id);
    if (!journal || journal->size() != 1U || journal->front().downloaded_bytes != 512U ||
        journal->front().status != "Downloading") return false;

    const auto default_queues = repository.list_queues();
    if (!default_queues || default_queues->size()!=1U || default_queues->front().id!="default") return false;
    vortex::QueueRecord custom_queue;
    custom_queue.id = "queue-test";
    custom_queue.name = "Test queue";
    custom_queue.max_concurrent = 4U;
    if (!repository.save_queue(custom_queue)) return false;
    const auto queues = repository.list_queues();
    if (!queues || queues->size()!=2U) return false;

    vortex::YtDlpProfileRecord profile;
    profile.id = "yt-dlp-official";
    profile.name = "Official yt-dlp releases";
    profile.source_type = "github_release";
    profile.repo_slug = "yt-dlp/yt-dlp";
    profile.pinned_tag = "stable";
    profile.include_prereleases = false;
    profile.local_binary_path = "/opt/vortex/yt-dlp";
    profile.installed_version = "2026.01.01";
    profile.is_active = true;
    if (!repository.save_ytdlp_profile(profile)) return false;
    const auto profiles = repository.list_ytdlp_profiles();
    if (!profiles || profiles->size() != 1U || profiles->front().repo_slug != "yt-dlp/yt-dlp" ||
        !profiles->front().is_active) return false;

    download.status = "Queued";
    if (!repository.save_download(download)) return false;
    vortex::QueueRecord disabled_queue;
    disabled_queue.id = "default";
    disabled_queue.name = "Default";
    disabled_queue.max_concurrent = 3U;
    disabled_queue.is_active = false;
    if (!repository.save_queue(disabled_queue)) return false;
    const auto blocked_claim = repository.claim_next_queued_download();
    if (!blocked_claim || *blocked_claim) return false;
    disabled_queue.is_active = true;
    if (!repository.save_queue(disabled_queue)) return false;
    const auto claimed = repository.claim_next_queued_download();
    if (!claimed || !*claimed || (**claimed).status != "Downloading") return false;
    if (!repository.reset_interrupted_downloads()) return false;
    const auto recovered_status = repository.get_download_status(download.id);
    if (!recovered_status || *recovered_status != "Queued") return false;
    const auto reclaimed = repository.claim_next_queued_download();
    if (!reclaimed || !*reclaimed) return false;
    if (!repository.update_download_progress(download.id, 256U, 1024U)) return false;
    if (!repository.update_download_status(download.id, "Paused")) return false;
    const auto paused = repository.get_download_status(download.id);
    if (!paused || *paused != "Paused") return false;
    if (!repository.update_download_status(download.id, "Queued")) return false;
    const auto records = repository.list_downloads();
    if (!records || records->size() != 1U || records->front().downloaded_bytes != 256U ||
        records->front().total_bytes != 1024U) return false;
    const bool matched = records->front().id == download.id &&
        records->front().downloaded_bytes == 256U &&
        records->front().total_bytes == 1024U &&
        records->front().status == "Queued";
    std::filesystem::remove(path, ignored);
    std::filesystem::remove(path.string() + "-wal", ignored);
    std::filesystem::remove(path.string() + "-shm", ignored);
    return matched;
}
}

int main() {
    if (!test_hls_and_dash_manifests()) {
        std::cerr << "HLS/DASH manifest parser test failed\\n";
        return 11;
    }
    if (!test_ytdlp_toolchain_verification_and_rollback()) {
        std::cerr << "yt-dlp toolchain verification/rollback test failed\\n";
        return 10;
    }
    if (!test_native_host_task_control()) {
        std::cerr << "Native Messaging task-control test failed\\n";
        return 12;
    }
    if (!test_segment_recovery_coordinator()) {
        std::cerr << "Segment recovery coordinator test failed\\n";
        return 9;
    }
    if (!test_ytdlp_metadata_parser()) {
        std::cerr << "yt-dlp metadata parser test failed\\n";
        return 8;
    }
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
