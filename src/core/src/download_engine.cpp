#include "vortex/download_engine.hpp"

#include "vortex/crash_safe_file_writer.hpp"
#include "vortex/filename_sanitizer.hpp"
#include "vortex/segment_scheduler.hpp"
#include "vortex/segment_checkpoint.hpp"
#include "vortex/url.hpp"

#include <curl/curl.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <thread>
#include <vector>

namespace vortex {
namespace {

struct CurlGlobal {
    CurlGlobal() { curl_global_init(CURL_GLOBAL_DEFAULT); }
    ~CurlGlobal() { curl_global_cleanup(); }
};

struct HeaderCollector {
    std::string accept_ranges;
    std::string content_range;
    std::string content_disposition;
    std::string etag;
    std::string last_modified;
};

std::string trim(std::string value) {
    const auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

size_t collect_headers(char* data, size_t size, size_t count, void* context) {
    const size_t bytes = size * count;
    auto* headers = static_cast<HeaderCollector*>(context);
    const std::string line(data, bytes);
    const auto separator = line.find(':');
    if (separator == std::string::npos) return bytes;
    const auto key = lower(trim(line.substr(0, separator)));
    const auto value = trim(line.substr(separator + 1));
    if (key == "accept-ranges") headers->accept_ranges = lower(value);
    else if (key == "content-range") headers->content_range = value;
    else if (key == "content-disposition") headers->content_disposition = value;
    else if (key == "etag") headers->etag = value;
    else if (key == "last-modified") headers->last_modified = value;
    return bytes;
}

size_t discard_body(char*, size_t size, size_t count, void*) {
    return size * count;
}

bool parse_content_range_total(const std::string& value, std::uint64_t& total) {
    const auto slash = value.rfind('/');
    if (slash == std::string::npos || slash + 1U >= value.size()) return false;
    const auto total_text = std::string_view(value).substr(slash + 1U);
    std::uint64_t parsed = 0;
    const auto result = std::from_chars(total_text.data(), total_text.data() + total_text.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != total_text.data() + total_text.size()) return false;
    total = parsed;
    return true;
}

std::string filename_from_disposition(const std::string& disposition) {
    if (disposition.empty()) return {};
    std::string candidate;
    const auto extended = lower(disposition).find("filename*=");
    if (extended != std::string::npos) {
        const auto start = disposition.find('=', extended);
        if (start != std::string::npos) {
            candidate = trim(disposition.substr(start + 1U));
            const auto quote = candidate.find("''");
            if (quote != std::string::npos) candidate = candidate.substr(quote + 2U);
            if (!candidate.empty() && candidate.front() == '"') candidate.erase(candidate.begin());
            if (!candidate.empty() && candidate.back() == '"') candidate.pop_back();
        }
    }
    if (candidate.empty()) {
        const auto regular = lower(disposition).find("filename=");
        if (regular != std::string::npos) {
            const auto start = disposition.find('=', regular);
            if (start != std::string::npos) {
                candidate = trim(disposition.substr(start + 1U));
                if (!candidate.empty() && candidate.front() == '"') candidate.erase(candidate.begin());
                const auto ending = candidate.find('"');
                if (ending != std::string::npos) candidate.resize(ending);
            }
        }
    }
    return candidate.empty() ? std::string{} : sanitize_filename(candidate);
}

int transfer_progress(void* context, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    const auto* cancel = static_cast<const std::atomic_bool*>(context);
    return cancel && cancel->load(std::memory_order_relaxed) ? 1 : 0;
}

void configure_common(CURL* handle, const std::string& url, long connect_timeout_ms, long transfer_timeout_ms, const std::atomic_bool* cancel = nullptr) {
    curl_easy_setopt(handle, CURLOPT_URL, url.c_str());
    curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(handle, CURLOPT_MAXREDIRS, 8L);
    curl_easy_setopt(handle, CURLOPT_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(handle, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT_MS, connect_timeout_ms);
    curl_easy_setopt(handle, CURLOPT_TIMEOUT_MS, transfer_timeout_ms);
    curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(handle, CURLOPT_USERAGENT, "VortexDM/0.1");
    if (cancel) {
        curl_easy_setopt(handle, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(handle, CURLOPT_XFERINFOFUNCTION, &transfer_progress);
        curl_easy_setopt(handle, CURLOPT_XFERINFODATA, const_cast<std::atomic_bool*>(cancel));
    }
}

Result<RemoteFileInfo> probe_url_impl(const std::string& url) {
    auto parsed = parse_http_url(url);
    if (!parsed) return std::unexpected(parsed.error());
    static CurlGlobal curl_global;
    (void)curl_global;

    HeaderCollector headers;
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> handle(curl_easy_init(), &curl_easy_cleanup);
    if (!handle) return std::unexpected(make_error("curl_init", "Could not initialize libcurl"));

    configure_common(handle.get(), url, 10000L, 15000L);
    curl_easy_setopt(handle.get(), CURLOPT_NOBODY, 1L);
    curl_easy_setopt(handle.get(), CURLOPT_HEADERFUNCTION, &collect_headers);
    curl_easy_setopt(handle.get(), CURLOPT_HEADERDATA, &headers);

    CURLcode curl_result = curl_easy_perform(handle.get());
    long status = 0;
    curl_easy_getinfo(handle.get(), CURLINFO_RESPONSE_CODE, &status);

    curl_off_t content_length = -1;
    curl_easy_getinfo(handle.get(), CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &content_length);
    char* effective_url = nullptr;
    curl_easy_getinfo(handle.get(), CURLINFO_EFFECTIVE_URL, &effective_url);

    RemoteFileInfo info;
    info.effective_url = effective_url != nullptr ? effective_url : url;
    info.response_code = status;
    info.etag = headers.etag;
    info.last_modified = headers.last_modified;
    info.filename = filename_from_disposition(headers.content_disposition);
    if (content_length >= 0) {
        info.total_bytes = static_cast<std::uint64_t>(content_length);
        info.size_known = true;
    }
    info.supports_ranges = headers.accept_ranges.find("bytes") != std::string::npos;

    const bool head_unavailable = curl_result != CURLE_OK || status == 405 || status == 501 ||
        status == 403 || !info.size_known || !info.supports_ranges;
    if (head_unavailable) {
        curl_easy_reset(handle.get());
        headers = HeaderCollector{};
        configure_common(handle.get(), url, 10000L, 15000L);
        curl_easy_setopt(handle.get(), CURLOPT_RANGE, "0-0");
        curl_easy_setopt(handle.get(), CURLOPT_WRITEFUNCTION, &discard_body);
        curl_easy_setopt(handle.get(), CURLOPT_HEADERFUNCTION, &collect_headers);
        curl_easy_setopt(handle.get(), CURLOPT_HEADERDATA, &headers);
        curl_result = curl_easy_perform(handle.get());
        curl_easy_getinfo(handle.get(), CURLINFO_RESPONSE_CODE, &status);
        curl_easy_getinfo(handle.get(), CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &content_length);
        curl_easy_getinfo(handle.get(), CURLINFO_EFFECTIVE_URL, &effective_url);
        if (effective_url != nullptr) info.effective_url = effective_url;
        info.response_code = status;
        if (!headers.etag.empty()) info.etag = headers.etag;
        if (!headers.last_modified.empty()) info.last_modified = headers.last_modified;
        if (info.filename.empty()) info.filename = filename_from_disposition(headers.content_disposition);
        std::uint64_t range_total = 0;
        if (curl_result == CURLE_OK && status == 206 && parse_content_range_total(headers.content_range, range_total)) {
            info.total_bytes = range_total;
            info.size_known = true;
            info.supports_ranges = true;
        } else if (!info.size_known && content_length >= 0) {
            info.total_bytes = static_cast<std::uint64_t>(content_length);
            info.size_known = true;
        }
    }

    if (curl_result != CURLE_OK) {
        return std::unexpected(make_error("probe_failed", curl_easy_strerror(curl_result)));
    }
    if (status >= 400) {
        return std::unexpected(make_error("http_error", "Server returned HTTP " + std::to_string(status)));
    }
    if (info.filename.empty()) {
        const auto slash = info.effective_url.find_last_of('/');
        const auto raw = slash == std::string::npos ? std::string("download") : info.effective_url.substr(slash + 1U);
        const auto query = raw.find_first_of("?#");
        info.filename = sanitize_filename(query == std::string::npos ? raw : raw.substr(0, query));
    }
    return info;
}

struct SequentialOutput {
    std::FILE* file{nullptr};
    std::uint64_t written{0};
    std::uint64_t total{0};
    const std::function<void(std::uint64_t, std::uint64_t)>* progress{nullptr};
};

struct FileCloser {
    void operator()(std::FILE* file) const noexcept {
        if (file != nullptr) {
            (void)std::fclose(file);
        }
    }
};

size_t write_sequential(char* data, size_t size, size_t count, void* context) {
    auto* output = static_cast<SequentialOutput*>(context);
    const auto elements = std::fwrite(data, size, count, output->file);
    const auto written = elements * size;
    output->written += static_cast<std::uint64_t>(written);
    if (output->progress && *output->progress) (*output->progress)(output->written, output->total);
    return written;
}

Result<void> download_sequential(const std::string& url, const std::filesystem::path& destination, const DownloadOptions& options, std::uint64_t total_bytes = 0U) {
    const auto native = destination.string();
    SequentialOutput output;
    output.progress = &options.progress_callback;
    output.total = total_bytes;
#ifdef _WIN32
    output.file = _wfopen(destination.c_str(), L"wb");
#else
    output.file = std::fopen(native.c_str(), "wb");
#endif
    if (output.file == nullptr) {
        return std::unexpected(make_error("file_open", "Could not open destination file"));
    }
    std::unique_ptr<std::FILE, FileCloser> file(output.file);
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> handle(curl_easy_init(), &curl_easy_cleanup);
    if (!handle) return std::unexpected(make_error("curl_init", "Could not initialize libcurl"));
    configure_common(handle.get(), url, options.connect_timeout_ms, options.transfer_timeout_ms, options.cancel_requested);
    curl_easy_setopt(handle.get(), CURLOPT_WRITEFUNCTION, &write_sequential);
    curl_easy_setopt(handle.get(), CURLOPT_WRITEDATA, &output);
    const auto result = curl_easy_perform(handle.get());
    if (result != CURLE_OK) {
        file.reset();
        std::error_code ignored;
        std::filesystem::remove(destination, ignored);
        return std::unexpected(make_error(options.cancel_requested && options.cancel_requested->load(std::memory_order_relaxed) ? "download_cancelled" : "network_error", curl_easy_strerror(result)));
    }
    long status = 0;
    curl_easy_getinfo(handle.get(), CURLINFO_RESPONSE_CODE, &status);
    if (status >= 400) {
        file.reset();
        std::error_code ignored;
        std::filesystem::remove(destination, ignored);
        return std::unexpected(make_error("http_error", "Server returned HTTP " + std::to_string(status)));
    }
    return {};
}

struct SegmentTransfer {
    CrashSafeFileWriter* writer{nullptr};
    ByteRange range;
    std::uint64_t received{0};
    std::shared_ptr<std::atomic_uint64_t> aggregate_progress;
    const std::function<void(std::uint64_t, std::uint64_t)>* progress{nullptr};
    std::uint64_t total_bytes{0};
    std::string content_range;
    bool write_failed{false};
};

size_t capture_content_range(char* data, size_t size, size_t count, void* context) {
    const auto bytes = size * count;
    auto* transfer = static_cast<SegmentTransfer*>(context);
    std::string line(data, bytes);
    const auto colon = line.find(':');
    if (colon != std::string::npos) {
        std::string name = line.substr(0U, colon);
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        if (name == "content-range") {
            auto value = line.substr(colon + 1U);
            const auto first = value.find_first_not_of(" \t");
            const auto last = value.find_last_not_of("\r\n \t");
            transfer->content_range = first == std::string::npos ? std::string{} : value.substr(first, last - first + 1U);
        }
    }
    return bytes;
}

bool valid_content_range(const SegmentTransfer& transfer) {
    const auto expected = "bytes " + std::to_string(transfer.range.start) + "-" +
        std::to_string(transfer.range.end) + "/";
    if (!transfer.content_range.starts_with(expected)) return false;
    const auto total_text = transfer.content_range.substr(expected.size());
    std::uint64_t total = 0;
    const auto parsed = std::from_chars(total_text.data(), total_text.data() + total_text.size(), total);
    return parsed.ec == std::errc{} && parsed.ptr == total_text.data() + total_text.size() &&
        (transfer.total_bytes == 0U || total == transfer.total_bytes);
}

size_t write_segment(char* data, size_t size, size_t count, void* context) {
    const size_t bytes = size * count;
    auto* transfer = static_cast<SegmentTransfer*>(context);
    if (bytes > transfer->range.size() - transfer->received) {
        transfer->write_failed = true;
        return 0;
    }
    const auto* begin = reinterpret_cast<const std::byte*>(data);
    const std::span<const std::byte> chunk(begin, bytes);
    auto written = transfer->writer->write(transfer->range.start + transfer->received, chunk);
    if (!written) {
        transfer->write_failed = true;
        return 0;
    }
    transfer->received += static_cast<std::uint64_t>(bytes);
    if (transfer->aggregate_progress) {
        const auto current = transfer->aggregate_progress->fetch_add(static_cast<std::uint64_t>(bytes), std::memory_order_relaxed) + static_cast<std::uint64_t>(bytes);
        if (transfer->progress && *transfer->progress) (*transfer->progress)(current, transfer->total_bytes);
    }
    return bytes;
}

Result<void> download_ranges(
    const std::string& url,
    const std::filesystem::path& destination,
    const RemoteFileInfo& info,
    const DownloadOptions& options) {
    auto ranges = make_byte_ranges(info.total_bytes, options.max_connections, options.minimum_segment_bytes);
    if (!ranges) return std::unexpected(ranges.error());

    CrashSafeFileWriter writer(destination);
    auto opened = writer.open(info.total_bytes);
    if (!opened) return opened;
    if (ranges->empty()) return writer.flush();

    std::unique_ptr<CURLM, decltype(&curl_multi_cleanup)> multi(curl_multi_init(), &curl_multi_cleanup);
    if (!multi) {
        writer.close();
        return std::unexpected(make_error("curl_multi_init", "Could not initialize libcurl multi handle"));
    }

    auto aggregate_progress = std::make_shared<std::atomic_uint64_t>(0U);
    std::unordered_map<CURL*, std::unique_ptr<SegmentTransfer>> transfers;
    for (const auto& range : *ranges) {
        auto handle = curl_easy_init();
        if (handle == nullptr) {
            for (auto& [easy, state] : transfers) {
                (void)state;
                curl_multi_remove_handle(multi.get(), easy);
                curl_easy_cleanup(easy);
            }
            writer.close();
            return std::unexpected(make_error("curl_init", "Could not initialize range request"));
        }
        auto state = std::make_unique<SegmentTransfer>();
        state->writer = &writer;
        state->range = range;
        state->aggregate_progress = aggregate_progress;
        state->progress = &options.progress_callback;
        state->total_bytes = info.total_bytes;
        const std::string range_header = std::to_string(range.start) + "-" + std::to_string(range.end);
        configure_common(handle, url, options.connect_timeout_ms, options.transfer_timeout_ms, options.cancel_requested);
        curl_easy_setopt(handle, CURLOPT_RANGE, range_header.c_str());
        curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, &write_segment);
        curl_easy_setopt(handle, CURLOPT_WRITEDATA, state.get());
        curl_easy_setopt(handle, CURLOPT_HEADERFUNCTION, &capture_content_range);
        curl_easy_setopt(handle, CURLOPT_HEADERDATA, state.get());
        curl_easy_setopt(handle, CURLOPT_PRIVATE, state.get());
        const auto add_result = curl_multi_add_handle(multi.get(), handle);
        if (add_result != CURLM_OK) {
            curl_easy_cleanup(handle);
            for (auto& [easy, existing] : transfers) {
                (void)existing;
                curl_multi_remove_handle(multi.get(), easy);
                curl_easy_cleanup(easy);
            }
            writer.close();
            return std::unexpected(make_error("curl_multi_add", curl_multi_strerror(add_result)));
        }
        transfers.emplace(handle, std::move(state));
    }

    int running = 0;
    CURLMcode multi_result = curl_multi_perform(multi.get(), &running);
    bool range_ignored = false;
    bool checkpoint_failed = false;
    std::string failure;
    while (multi_result == CURLM_OK && (running > 0 || !transfers.empty())) {
        int messages_left = 0;
        while (auto* message = curl_multi_info_read(multi.get(), &messages_left)) {
            if (message->msg != CURLMSG_DONE) continue;
            const auto found = transfers.find(message->easy_handle);
            if (found == transfers.end()) continue;
            long status = 0;
            curl_easy_getinfo(message->easy_handle, CURLINFO_RESPONSE_CODE, &status);
            const auto& state = *found->second;
            const auto expected = state.range.size();
            if (status == 200) {
                range_ignored = true;
            } else if (message->data.result != CURLE_OK) {
                failure = curl_easy_strerror(message->data.result);
            } else if (status != 206) {
                failure = "Range request returned HTTP " + std::to_string(status);
            } else if (state.write_failed || state.received != expected) {
                failure = "Range response length did not match the requested byte interval";
            }
            curl_multi_remove_handle(multi.get(), message->easy_handle);
            curl_easy_cleanup(message->easy_handle);
            transfers.erase(found);
            if (!failure.empty() || range_ignored) break;
        }
        if (!failure.empty() || range_ignored || transfers.empty()) break;
        multi_result = curl_multi_poll(multi.get(), nullptr, 0, 1000, nullptr);
        if (multi_result == CURLM_OK) multi_result = curl_multi_perform(multi.get(), &running);
    }

    for (auto& [easy, state] : transfers) {
        (void)state;
        curl_multi_remove_handle(multi.get(), easy);
        curl_easy_cleanup(easy);
    }
    transfers.clear();

    if (multi_result != CURLM_OK && failure.empty()) failure = curl_multi_strerror(multi_result);
    if (range_ignored) {
        writer.close();
        std::error_code ignored;
        std::filesystem::remove(destination, ignored);
        if (options.progress_callback) options.progress_callback(0U, info.total_bytes);
        return download_sequential(url, destination, options, info.total_bytes);
    }
    if (!failure.empty()) {
        const bool cancelled = options.cancel_requested && options.cancel_requested->load(std::memory_order_relaxed);
        writer.close();
        if (!cancelled) {
            std::error_code ignored;
            std::filesystem::remove(destination, ignored);
            if (options.progress_callback) options.progress_callback(0U, info.total_bytes);
        }
        return std::unexpected(make_error(cancelled ? "download_cancelled" : checkpoint_failed ? "segment_checkpoint" : "range_download", failure));
    }
    return writer.flush();
}

} // namespace


namespace {
struct MemoryOutput {
    std::string data;
    std::size_t maximum_bytes{0};
    bool exceeded{false};
};
size_t write_memory(char* data, size_t size, size_t count, void* context) {
    auto* output = static_cast<MemoryOutput*>(context);
    if (size != 0U && count > (std::numeric_limits<std::size_t>::max)() / size) {
        output->exceeded = true;
        return 0U;
    }
    const auto bytes = size * count;
    if (bytes > output->maximum_bytes - (std::min)(output->data.size(), output->maximum_bytes)) {
        output->exceeded = true;
        return 0U;
    }
    output->data.append(data, bytes);
    return bytes;
}
}

Result<std::string> fetch_resource(
    const std::string& url, std::size_t maximum_bytes, const DownloadOptions& options) {
    if (maximum_bytes == 0U)
        return std::unexpected(make_error("resource_limit", "Maximum resource size must be greater than zero"));
    if (options.cancel_requested && options.cancel_requested->load(std::memory_order_relaxed))
        return std::unexpected(make_error("download_cancelled", "Resource request was cancelled before it started"));
    auto parsed = parse_http_url(url);
    if (!parsed) return std::unexpected(parsed.error());
    static CurlGlobal curl_global;
    (void)curl_global;
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> handle(curl_easy_init(), &curl_easy_cleanup);
    if (!handle) return std::unexpected(make_error("curl_init", "Could not initialize libcurl resource handle"));
    MemoryOutput output;
    output.maximum_bytes = maximum_bytes;
    configure_common(handle.get(), url, options.connect_timeout_ms, options.transfer_timeout_ms, options.cancel_requested);
    curl_easy_setopt(handle.get(), CURLOPT_WRITEFUNCTION, &write_memory);
    curl_easy_setopt(handle.get(), CURLOPT_WRITEDATA, &output);
    const auto result = curl_easy_perform(handle.get());
    if (output.exceeded)
        return std::unexpected(make_error("resource_limit", "Remote resource exceeded the configured size limit"));
    if (result != CURLE_OK) {
        const bool cancelled = options.cancel_requested && options.cancel_requested->load(std::memory_order_relaxed);
        return std::unexpected(make_error(cancelled ? "download_cancelled" : "resource_fetch",
            cancelled ? "Resource request was cancelled" : curl_easy_strerror(result)));
    }
    long status = 0;
    curl_easy_getinfo(handle.get(), CURLINFO_RESPONSE_CODE, &status);
    if (status >= 400)
        return std::unexpected(make_error("http_error", "Server returned HTTP " + std::to_string(status)));
    return output.data;
}

Result<RemoteFileInfo> probe_url(const std::string& url) {
    return probe_url_impl(url);
}

namespace {
bool retryable_transfer_error(const Error& error) {
    if (error.code == "network_error" || error.code == "probe_failed" ||
        error.code == "curl_multi_init" || error.code == "curl_multi_add") return true;
    if (error.code == "range_download" || error.code == "http_error") {
        const bool client_error = error.message.find("HTTP 4") != std::string::npos;
        if (client_error) return error.message.find("HTTP 408") != std::string::npos ||
            error.message.find("HTTP 429") != std::string::npos;
        return error.message.find("HTTP 5") != std::string::npos || error.code == "range_download";
    }
    return false;
}
}

Result<std::uint64_t> download_range_to_writer(
    const std::string& url, const ByteRange& range, std::uint64_t totalBytes, CrashSafeFileWriter& writer,
    const DownloadOptions& options) {
    if (range.end < range.start)
        return std::unexpected(make_error("invalid_range", "Range end must not be before range start"));
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> handle(curl_easy_init(), &curl_easy_cleanup);
    if (!handle) return std::unexpected(make_error("curl_init", "Could not initialize libcurl range handle"));
    SegmentTransfer transfer;
    transfer.writer = &writer;
    transfer.range = range;
    transfer.total_bytes = totalBytes;
    configure_common(handle.get(), url, options.connect_timeout_ms, options.transfer_timeout_ms, options.cancel_requested);
    const std::string range_spec = std::to_string(range.start) + "-" + std::to_string(range.end);
    curl_easy_setopt(handle.get(), CURLOPT_RANGE, range_spec.c_str());
    curl_easy_setopt(handle.get(), CURLOPT_WRITEFUNCTION, &write_segment);
    curl_easy_setopt(handle.get(), CURLOPT_WRITEDATA, &transfer);
    curl_easy_setopt(handle.get(), CURLOPT_HEADERFUNCTION, &capture_content_range);
    curl_easy_setopt(handle.get(), CURLOPT_HEADERDATA, &transfer);
    const CURLcode result = curl_easy_perform(handle.get());
    if (result != CURLE_OK) {
        return std::unexpected(make_error(options.cancel_requested && options.cancel_requested->load(std::memory_order_relaxed)
            ? "download_cancelled" : "range_recovery", curl_easy_strerror(result)));
    }
    long status = 0;
    curl_easy_getinfo(handle.get(), CURLINFO_RESPONSE_CODE, &status);
    if (status != 206)
        return std::unexpected(make_error("range_recovery", "Server did not honor recovery range request (HTTP " + std::to_string(status) + ")"));
    if (!valid_content_range(transfer))
        return std::unexpected(make_error("range_recovery", "Recovery Content-Range did not match the requested interval"));
    if (transfer.received != range.size())
        return std::unexpected(make_error("range_recovery", "Recovery range response was incomplete"));
    return transfer.received;
}

Result<void> download_file(
    const std::string& url,
    const std::filesystem::path& destination,
    const DownloadOptions& options) {
    if (options.cancel_requested && options.cancel_requested->load(std::memory_order_relaxed))
        return std::unexpected(make_error("download_cancelled", "Download was cancelled before it started"));
    const std::size_t attempts = std::min<std::size_t>(options.max_retries, 10U) + 1U;
    Error last_error = make_error("download_failed", "Download did not start");
    for (std::size_t attempt = 0; attempt < attempts; ++attempt) {
        auto info = probe_url(url);
        if (!info) {
            last_error = info.error();
        } else {
            if (options.stream_info_callback) {
                auto saved_stream = options.stream_info_callback(*info);
                if (!saved_stream) return saved_stream;
            }
            if (attempt > 0U && options.reset_segment_checkpoints_callback) {
                auto reset = options.reset_segment_checkpoints_callback();
                if (!reset) return reset;
            }
            if (options.progress_callback) options.progress_callback(0U, info->total_bytes);
            Result<void> transfer;
            if (info->size_known && info->supports_ranges && options.max_connections > 1U && info->total_bytes > 0U) {
                transfer = download_ranges(info->effective_url, destination, *info, options);
            } else {
                transfer = download_sequential(info->effective_url, destination, options, info->total_bytes);
            }
            if (transfer) return {};
            last_error = transfer.error();
        }
        if ((options.cancel_requested && options.cancel_requested->load(std::memory_order_relaxed)) ||
            attempt + 1U >= attempts || !retryable_transfer_error(last_error)) {
            return std::unexpected(last_error);
        }
        std::error_code ignored;
        std::filesystem::remove(destination, ignored);
        if (options.progress_callback) options.progress_callback(0U, 0U);
        const auto exponent = std::min<std::size_t>(attempt, 6U);
        const std::uint64_t multiplier = std::uint64_t{1} << exponent;
        const std::uint64_t delay = options.retry_backoff_ms > 30000U / multiplier
            ? 30000U : std::min<std::uint64_t>(options.retry_backoff_ms * multiplier, 30000U);
        std::this_thread::sleep_for(std::chrono::milliseconds(delay));
    }
    return std::unexpected(last_error);
}

} // namespace vortex
